

|  |
| :---- |

**XENA LaaS**

**Debug SDK — Architecture Proposal**

*Deterministic Execution Analysis for the Quants Team*

| Classification: Internal — Architecture Status: Proposal / Review Audience: XENA Team · Quants Team · Platform Architecture |
| :---- |

|  |
| :---- |

# **1\. Introduction**

This document defines the proposed XENA Debug SDK as a solution to support library debugging and deterministic execution analysis requested by the Quants team, while preserving the architectural principles and boundaries of the XENA LaaS platform.

### **This proposal addresses:**

* The Quants' requirement for deterministic debugging and execution visibility

* The architectural constraints of XENA as a contract-driven orchestrator

* The risks of introducing API-based data access into XENA

* A secure, scalable, and decoupled alternative based on a client-side SDK

# **2\. Problem Statement**

The Quants team requires deterministic reproduction of library executions, with access to input data, output data, computation context, and debugging capabilities aligned with production executions.

### **Initial proposals included:**

* Exposing HTTP services directly from XENA

* Returning input/output/computation data from XENA API responses

| ⚠  Architectural Concerns  These proposals violate XENA's responsibility boundaries, reintroduce a deprecated API-based architecture, tightly couple XENA to library semantics, and create data governance risks through uncontrolled data exposure. |
| :---- |

# **3\. Architectural Position**

| 🏛  Design Statement  XENA is a contract-driven, event-based orchestrator over a data-plane (S3). It is not a data delivery service. |
| :---- |

### **XENA Responsibilities**

* Ingest Parquet artifacts from upstream producers

* Enrich and route executions based on contract metadata

* Orchestrate GRID library execution

* Persist outputs and tracking artifacts to S3

### **XENA Explicitly Does NOT**

* Interpret input data or library semantics

* Execute or debug GRID library logic

* Expose data via HTTP APIs

* Own or distribute data to consumers

# **4\. Design Principles**

The solution must satisfy all of the following constraints:

* Preserve XENA architecture — no API layer reintroduction

* Respect data governance boundaries — all access via IAM-controlled S3

* Maintain library agnosticism — SDK must not embed library schemas

* Support independent evolution of GRID libraries without SDK changes

* Enable deterministic debugging without any XENA platform modifications

# **5\. Proposed Solution — XENA Debug SDK**

## **5.1 Overview**

The XENA SDK is a client-side tool that enables Quants to access execution artifacts, reconstruct execution context, analyze computation results, and debug libraries using real production execution data.

The SDK operates entirely outside XENA, interacting directly with the S3 data plane and execution artifacts (Parquet files and manifest.json).

| 🔑  Key Principle  The SDK provides access and interpretation. XENA provides neither. |
| :---- |

# **6\. SDK Architecture**

## **6.1 Functional Responsibilities**

The SDK provides a focused set of capabilities:

* Authentication and credential resolution (SSO / STS)

* S3 path resolution based on the XENA execution contract

* Access to input Parquet, output Parquet, error artifacts, and manifest.json

* Parquet decoding and payload extraction utilities

* Execution inspection and local reproducibility support

## **6.2 Execution Flow**

The table below describes the end-to-end flow from a Quant's debug request to artifact retrieval and analysis:

| \# | Actor | Action |
| :---- | :---- | :---- |
| **1** | **User / Quant** | Invokes SDK with execution Job ID and desired artifact type |
| **2** | **SDK** | Resolves AWS credentials via SSO or STS AssumeRole |
| **3** | **SDK → S3** | Constructs artifact paths from XENA contract; fetches Parquet / manifest |
| **4** | **SDK** | Decodes Parquet; extracts payload; exposes typed execution context |
| **5** | **SDK (optional)** | Generates signed URLs for time-limited, direct artifact access |
| **6** | **User / Quant** | Performs local analysis, diff, or GRID library re-execution for debugging |

# **7\. Language and Distribution Strategy**

## **7.1 Rationale for a Compiled SDK**

The SDK must protect internal logic, integrate cleanly with enterprise security tooling, and minimize runtime dependencies. Compiled distribution satisfies all three requirements while remaining opaque to consumers.

## **7.2 Language Comparison**

| Criterion | C++ | Java | Python |
| :---- | :---- | :---- | :---- |
| **Performance** | Native / optimal | JVM overhead | Interpreted / slower |
| **Distribution** | Compiled binary (opaque) | JAR / class files | Source or wheel |
| **JVM Required** | No | Yes | No (CPython) |
| **Dev Speed** | Slower | Fast (XENA team expertise) | Fast |
| **Quant Ecosystem** | Primary fit | Secondary fit | Optional wrapper |
| **Recommendation** | ✔ Primary SDK | Fallback | Future binding |

## **7.3 Recommended Strategy**

* Primary SDK: C++ — compiled binary, native performance, no JVM dependency

* Secondary SDK: Java — fallback leveraging existing XENA team expertise

* Optional future: Python wrapper via C++ bindings for data-science workflows

# **8\. Security Model**

| 🔒  Design Principle  The SDK operates under the caller's identity. It does not act as a proxy and grants no permissions of its own. |
| :---- |

## **8.1 Authentication & Authorization Mechanisms**

| Mechanism | Description |
| :---- | :---- |
| **AWS SSO** | User authenticates via aws sso login; short-lived credentials issued automatically |
| **STS AssumeRole** | SDK calls sts:AssumeRole to obtain scoped, time-limited session credentials |
| **IAM Policies** | Fine-grained actions (s3:GetObject) restricted per role and resource ARN |
| **Bucket Policies** | S3 bucket-level controls enforce data-lake boundaries independent of SDK |
| **ABAC / Tagging** | Attribute-based access control via resource tags for multi-tenant isolation (optional) |
| **Signed URLs** | Generated client-side using caller credentials; time-limited, no extra permissions granted |

## **8.2 Key Property**

| ✓  Access Guarantee  The SDK cannot grant access beyond what the caller's IAM identity already permits. No XENA involvement in credential issuance or data delivery. |
| :---- |

# **9\. Data Access and Governance**

## **9.1 Governance Constraints**

* XENA does not own data

* XENA does not expose data

* XENA does not intermediate data delivery

## **9.2 Architectural Rule**

| 📐  Rule  All data access must occur directly through the Data Lake (S3) under the caller's IAM identity. No data-return APIs, no data transformation inside XENA, no duplication of access paths. |
| :---- |

# **10\. Data Access Model**

## **10.1 Reference-Based Response**

Instead of returning data directly, the SDK resolves and returns S3 references and optionally generates signed URLs for direct artifact access:

| // SDK response model (TypeScript-style for clarity) |
| :---- |
| interface ExecutionArtifacts { |
|   jobId:        string; |
|   inputPath:    string;   // s3://xena-data-lake-input/.../input.parquet |
|   outputPath:   string;   // s3://xena-data-lake-output/.../results.parquet |
|   errorPath:    string;   // s3://xena-data-lake-errors/.../errors.json |
|   manifestPath: string;   // s3://xena-data-lake-output/.../manifest.json |
|   signedUrls?: { |
|     input:    string;     // Pre-signed URL; expires per SDK config (default 1h) |
|     output:   string; |
|     manifest: string; |
|   }; |
| } |

## **10.2 Signed URL Generation**

Signed URLs are generated client-side by the SDK using the AWS SDK and the caller's credentials. They are time-limited (configurable, default 1 hour), grant no additional permissions, and require no XENA involvement.

# **11\. manifest.json — Central Role**

## **11.1 Purpose**

The manifest.json is the authoritative execution record persisted by GRID upon job completion. It is the single extensible surface for observability and debugging metadata — no XENA API changes are required to add new debug fields.

## **11.2 Schema**

| { |
| :---- |
|   "jobId":          "j-8f3a21c9", |
|   "libraryVersion": "1.2.3", |
|   "status":         "COMPLETED",       // COMPLETED | FAILED | PARTIAL |
|   "startTime":      "2024-06-01T08:00:00Z", |
|   "endTime":        "2024-06-01T08:02:47Z", |
|   "durationMs":     167432, |
|   "inputChecksum":  "sha256:a3f8...",  // For determinism verification |
|   "outputChecksum": "sha256:9c12...", |
|   "metrics": { |
|     "rowsProcessed": 1500000, |
|     "rowsErrored":   23, |
|     "memoryPeakMb":  4096 |
|   }, |
|   "debugInfo": {                       // Extensible — owned by GRID |
|     "libraryTrace": "...", |
|     "configSnapshot": { "param": "value" } |
|   } |
| } |

| 📌  Strategic Note  Debug data belongs to GRID and is surfaced via the manifest — not via XENA APIs. The manifest is the single extension point; adding new debug fields requires no platform changes. |
| :---- |

# **12\. Optional: Local Reproducibility**

The SDK optionally supports full offline reproducibility for deterministic debugging:

* Download input artifacts from S3 to a local path

* Extract the payload and configuration snapshot from the manifest

* Execute GRID libraries locally against the downloaded inputs

* Compare local outputs against the persisted S3 results

| // Pseudocode — SDK local reproducibility API (C++) |
| :---- |
| auto session \= XenaSDK::Session::fromSSO(); |
|  |
| // Resolve and download artifacts for a given job |
| auto artifacts \= session.resolveJob("j-8f3a21c9"); |
| artifacts.downloadInputs("/tmp/xena-debug/j-8f3a21c9/"); |
|  |
| // Extract config snapshot from manifest |
| auto cfg \= artifacts.manifest().configSnapshot(); |
|  |
| // Re-execute GRID library locally for determinism check |
| auto result \= GridRunner::execute(cfg, "/tmp/xena-debug/j-8f3a21c9/input.parquet"); |
|  |
| // Compare checksums |
| bool deterministic \= (result.checksum() \== artifacts.manifest().outputChecksum()); |

# **13\. Approach Comparison**

The table below summarizes the trade-offs between the rejected API-based approach and the proposed SDK approach across all key dimensions:

| Aspect | API-Based Approach | SDK Approach |
| :---- | :---- | :---- |
| **Architecture Impact** | High — requires new endpoints, service layer, and versioning | None — operates entirely outside XENA |
| **Coupling** | High — XENA must understand library semantics | Low — SDK is independent of XENA internals |
| **Data Governance Risk** | High — data returned via API bypasses lake controls | Low — access enforced by IAM/bucket policies |
| **Scalability** | Limited — XENA becomes a data-delivery bottleneck | High — direct S3 access, fully serverless |
| **Ownership Clarity** | Blurred — XENA partially owns data interpretation | Clear — GRID owns data; SDK provides tooling |
| **Operational Cost** | High — new services to deploy, monitor, and maintain | Minimal — no backend; credentials from caller |
| **Security Model** | Custom OAuth / proxy patterns required inside XENA | Standard AWS SSO / STS — no custom flows |
| **Library Agnosticism** | Broken — API shapes must know library output schemas | Preserved — SDK reads generic Parquet artifacts |

# **14\. Final Position**

| 📋  Decision Record  XENA will not implement data-return APIs. XENA will not interpret or expose library data. Debugging capabilities are provided exclusively via the SDK and manifest artifacts. |
| :---- |

# **15\. Conclusion**

The XENA Debug SDK provides a complete, secure, and scalable solution to address the Quants team's debugging requirements while fully preserving:

* Architectural integrity — XENA remains a contract-driven orchestrator

* Data governance — all access enforced by IAM and bucket policies

* Platform scalability — no backend services, fully serverless

* Separation of concerns — GRID owns data; SDK provides tooling

**XENA remains a contract-driven orchestrator.** *Debugging, inspection, and interpretation are enabled through artifacts and tooling — not through platform-level APIs.*