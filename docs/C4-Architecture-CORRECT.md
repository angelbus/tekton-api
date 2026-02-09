# XENA MVP1 - C4 Architecture Documentation
## Correct Design Based on Architectural Review

---

## Executive Summary

This document describes the **CORRECT** C4 architecture for XENA MVP1, addressing the architectural deviations identified in the performance review. The design emphasizes:

✅ **Early validation** (fail-fast principle)  
✅ **Streaming architecture** (no memory buffering)  
✅ **Proper Redis usage** (references only, not payloads)  
✅ **Clear separation of concerns** (Lambda vs EKS responsibilities)  
✅ **Parquet-based data flow** (not Protobuf)  

---

## KEY ARCHITECTURAL PRINCIPLES

### Lambda (XENA Loader) - LIGHTWEIGHT EDGE GATEWAY
- ✅ Stream Parquet from S3 (NO buffering)
- ✅ EARLY schema validation (reject invalid data HERE)
- ✅ Edge idempotency enforcement
- ✅ Store S3 URI reference in Redis (NOT full file)
- ✅ Target execution: <2 seconds (not 30+ seconds!)
- ❌ NO full object downloads
- ❌ NO bulk data in Redis

### EKS (Calibration Service) - BUSINESS ORCHESTRATION
- ✅ Defensive idempotency (backend protection)
- ✅ Retrieve Parquet from S3 by URI
- ✅ Business logic orchestration
- ✅ Output lifecycle management
- ✅ Auditability & traceability
- ✅ Isolate GRID integration
- ❌ NOT a simple pass-through

### Redis - SMALL STATE ONLY
- ✅ Idempotency keys
- ✅ Correlation IDs
- ✅ S3 URI references
- ✅ Processing metadata
- ✅ Typical payload: ~1-2 KB
- ❌ NO full Parquet files
- ❌ NO MB-sized payloads

### S3 - SOURCE OF TRUTH
- ✅ Input: Raw Parquet files
- ✅ Output: Calibrated Parquet results
- ✅ System of record for all data
- ✅ Stream-friendly access patterns

---

## C4 LEVEL 1: CONTEXT DIAGRAM

### Description
Shows XENA system in context of external actors and systems.

### Systems

#### **XENA System** (Software System - Main)
Market data calibration platform. Processes Parquet datasets, calibrates models, produces PnL, Greeks, risk metrics.

#### External Systems:
1. **Sigma/DataLake** - Provides/consumes Parquet datasets via DaaS API
2. **Markets Platforms** - Risk & reporting systems consume calibrated metrics
3. **Batch Schedulers** - Control-M, Airflow trigger calibration jobs
4. **SCIB Data Lake** - On-premise raw data storage
5. **Observability Tools** - Grafana, CloudWatch for monitoring

#### Human Actors:
1. **Risk Analysts/Model Validation** - Require reliable, replayable results
2. **Operations/Run-the-Bank** - Manage batches, incidents, SLAs
3. **Developers/Quant Teams** - Test models in lower environments

### Key Relationships:
- Batch Schedulers → XENA (trigger calibration via HTTPS)
- SCIB/Sigma → XENA (provide Parquet datasets via S3)
- XENA → Markets (provide calibrated metrics via Sigma/DaaS)
- XENA → Observability (send logs, metrics, traces)

---

## C4 LEVEL 2: CONTAINER DIAGRAM

### XENA System Containers (AWS Public Cloud)

#### 1. **API Gateway** [AWS API Gateway]
- HTTPS entry point
- JWT authentication via ACM
- Routes to XENA Loader

#### 2. **XENA Loader** [AWS Lambda - Python 3.12]
**CRITICAL: Lightweight edge gateway**
- Streams Parquet from S3 (NO buffering)
- Early schema validation (fail-fast)
- Edge idempotency enforcement
- Stores S3 URI reference in Redis (NOT full file)
- Target execution: <2 seconds
- Triggers Calibration Service

#### 3. **S3 Input Bucket** [AWS S3]
- Raw Parquet files: Jacobians, configs, scenarios
- Source from SCIB Data Lake (via GSNet)
- Source from Sigma/DataLake (via S3 API)

#### 4. **XENA Cache** [ElastiCache Redis]
**CRITICAL: Small references only**
- Idempotency keys
- Correlation IDs
- S3 URIs (not payloads!)
- Processing metadata
- TTL: 24-48 hours
- Typical entry: ~1-2 KB

#### 5. **Secrets Manager** [AWS Secrets Manager]
- Access tokens, credentials

#### 6. **ACM** [AWS ACM]
- Certificate management for JWT

#### 7. **KMS** [AWS KMS]
- Encryption key management

#### 8. **EKS Cluster** (XENA Namespace)
Contains 3 pods:

##### 8a. **SCIB Security Gateway** [EKS Pod]
- JWT verification
- Request routing to Calibration Service

##### 8b. **Calibration Service** [EKS Pod - Java/Spring Boot]
**CRITICAL: Business orchestration layer**
- Defensive idempotency (backend protection)
- Retrieves Parquet from S3 by URI
- Business logic coordination
- Invokes Grid Connector
- Output lifecycle management
- Auditability & traceability

##### 8c. **Grid Connector** [EKS Pod]
- HTTPS interface to MTO GRID (AWS)
- Isolates external GRID dependency
- Returns calibration results

#### 9. **S3 Output Bucket** [AWS S3]
- Calibrated Parquet: PnL, Greeks, exposures
- Consumed by Markets via Sigma/DataLake

#### 10. **ECR** [AWS ECR]
- Container image registry

#### 11. **CloudWatch** [AWS CloudWatch]
- Centralized logging, metrics, traces

### Key Container Relationships:
- Batch Scheduler → API Gateway (trigger)
- API Gateway → XENA Loader (invoke)
- XENA Loader ⇄ S3 Input (STREAM Parquet)
- XENA Loader → XENA Cache (store S3 URI reference)
- XENA Loader → SCIB Gateway (trigger with key)
- SCIB Gateway → Calibration Service (forward request)
- Calibration Service ⇄ S3 Input (retrieve Parquet by URI)
- Calibration Service → Grid Connector (request calibration)
- Grid Connector ⇄ MTO GRID External (execute)
- Calibration Service → S3 Output (write calibrated Parquet)
- S3 Output → Markets (consume via DaaS)

---

## C4 LEVEL 3: COMPONENT DIAGRAM - XENA LOADER (LAMBDA)

### Container: XENA Loader [AWS Lambda - Python 3.12]

**Architectural Role:** Lightweight Edge Gateway

### Components:

#### 1. **Event Handler**
- Receives trigger from API Gateway
- Extracts S3 URI, metadata, correlation ID
- Initiates processing pipeline

#### 2. **Edge Idempotency Checker**
- Generate UUID idempotency key
- Check Redis for existing key
- **ABORT if duplicate detected**
- Protects against S3 at-least-once delivery
- Prevents duplicate downstream processing

#### 3. **S3 Streaming Reader**
- **Uses ResponseInputStream API**
- **NO in-memory buffering**
- Memory-efficient for large Parquet (20+ MB)
- Streams directly to validator
- Target: ~500ms for streaming setup

#### 4. **Parquet Schema Validator**
**CRITICAL: EARLY VALIDATION (Fail-Fast)**
- Parse Parquet schema from stream
- Validate completeness (columns, types, expected fields)
- **REJECT INVALID DATA HERE**
- Do NOT pass invalid data downstream
- Saves EKS resources
- Prevents Redis pollution
- Target: ~500ms validation time

#### 5. **Metadata Extractor**
- Extract calibration metadata from validated Parquet
- Generate unique correlation key
- Prepare for downstream processing

#### 6. **Reference Object Builder**
**CRITICAL: Build lightweight reference**
- S3 URI (NOT full payload!)
- Idempotency key
- Validation status
- Metadata
- Correlation ID
- **Typical size: ~1-2 KB** (not MB!)

#### 7. **Redis Reference Writer**
- Store REFERENCE only (~1 KB)
- **NOT full Parquet file**
- Fast write (<100ms typical)
- TTL: 24-48 hours
- Enables correlation & idempotency

#### 8. **Calibration Service Trigger**
- Invoke SCIB Gateway
- Pass reference key and idempotency key
- **NOT payload**
- HTTPS call to EKS

#### 9. **Error Handler & Early Rejection**
- Log validation failures
- Return appropriate HTTP error
- **Do NOT pollute Redis or trigger downstream**
- Early rejection saves resources

#### 10. **Observability Logger**
- Emit structured logs
- Metrics: duration, file size, validation status
- Traces to CloudWatch
- Performance monitoring

### Component Flow:

```
API Gateway 
  → Event Handler
    → Edge Idempotency Checker ⇄ Redis (check key)
    → S3 Streaming Reader ⇄ S3 Input (STREAM Parquet)
      → Parquet Schema Validator
        → [VALID] → Metadata Extractor
          → Reference Object Builder
            → Redis Reference Writer → Redis (store ~1KB reference)
            → Calibration Service Trigger → SCIB Gateway
        → [INVALID] → Error Handler (REJECT, return error)
  
  All components → Observability Logger → CloudWatch
```

### Performance Targets:
- **Cold start:** ~1s
- **Streaming validation:** ~500ms
- **Redis write (reference):** <100ms
- **Downstream trigger:** ~200ms
- **TOTAL:** <2 seconds typical
- **NOT 30+ seconds!**

### Critical Success Factors:
✅ Stream from S3 (no buffering)
✅ Validate early (fail-fast)
✅ Store references only in Redis
✅ Reject invalid data at edge
✅ Fast, lightweight execution

---

## C4 LEVEL 3: COMPONENT DIAGRAM - CALIBRATION SERVICE (EKS)

### Container: Calibration Service [EKS Pod - Java/Spring Boot]

**Architectural Role:** Business Orchestration & Platform Protection

**NOT a simple pass-through!** Purpose is to:
- Enforce defensive idempotency
- Manage output lifecycle
- Provide auditability & traceability
- Isolate platform from GRID integration
- Maintain stakeholder contracts

### Components:

#### 1. **Request Handler**
- HTTPS REST endpoint
- Receives calibration request from SCIB Gateway
- Extracts idempotency key from header (X-Idempotency-Key)
- Extracts reference key for S3 retrieval

#### 2. **Defensive Idempotency Checker**
**CRITICAL: Backend protection layer**
- Extract idempotency key from request header
- Check Redis for existing processing
- **ABORT if already processed**
- Protects against:
  - Network retries
  - Duplicate triggers
  - Replay attacks
- **Different from edge idempotency!**
  - Edge: Prevents duplicate invocations
  - Defensive: Protects against retries/failures

#### 3. **Reference Resolver**
- Retrieve reference object from Redis by key
- Extract S3 URI
- Extract metadata
- Validate reference exists and is valid

#### 4. **S3 Parquet Retriever**
- Extract S3 URI from reference
- Retrieve Parquet file from S3 Input Bucket
- Can stream if needed for large files
- **S3 is source of truth** (not Redis)

#### 5. **Calibration Orchestrator**
**Business logic coordinator:**
- Prepare calibration request payload
- Manage workflow state
- Handle retries with circuit breakers
- Coordinate with Grid Invoker
- Process responses
- Error handling & recovery

#### 6. **GRID Invoker**
- Call Grid Connector (separate EKS pod)
- Pass calibration request
- **Isolates external dependency**
- Handles GRID-specific protocols
- Returns results to orchestrator

#### 7. **Results Processor**
- Process calibrated results from GRID
- Transform into output format (Parquet)
- Validate completeness
- Apply business rules
- Prepare for storage

#### 8. **Output Lifecycle Manager**
**CRITICAL: Platform responsibility**
- Write calibrated Parquet to S3 Output Bucket
- Manage output contract/schema
- Update processing state in Redis
- Mark idempotency key as complete
- Store result location (S3 URI)
- Ensure auditability trail
- Handle output errors gracefully

#### 9. **Audit & Traceability Logger**
- Record complete processing history
- Track all state transitions
- Link request → processing → output
- Enable replay & debugging
- Compliance & regulatory requirements

#### 10. **Error Handler & Circuit Breaker**
- Catch exceptions at each stage
- Implement circuit breaker for GRID calls
- Manage retry logic with backoff
- Trigger alerts for failures
- Prevent cascade failures

#### 11. **Observability Agent**
- Emit detailed logs
- Business metrics (processing time, success rate)
- Technical metrics (latency, errors)
- Distributed traces
- Link to Lambda idempotency keys

### Component Flow:

```
SCIB Gateway
  → Request Handler (extract idempotency key + reference key)
    → Defensive Idempotency Checker ⇄ Redis (check if processed)
      → [NEW] → Reference Resolver ⇄ Redis (get S3 URI + metadata)
        → S3 Parquet Retriever ⇄ S3 Input (retrieve Parquet by URI)
          → Calibration Orchestrator
            → GRID Invoker → Grid Connector (external pod) ⇄ MTO GRID
            ← Results from GRID
          → Results Processor (transform, validate)
            → Output Lifecycle Manager
              → Write to S3 Output (calibrated Parquet)
              → Update Redis (mark complete, store output URI)
              → Audit & Traceability Logger
      → [ALREADY PROCESSED] → Return existing result URI

  All components → Observability Agent → CloudWatch
  All components → Error Handler (on exceptions)
```

### Separation of Concerns:

| Component | Responsibility |
|-----------|---------------|
| **Calibration Service** | Platform protection, lifecycle, orchestration, contracts |
| **Grid Connector** | External system integration isolation |

**This separation is DELIBERATE:**
- CalibrationService exists to protect the platform from external integration
- GRIDConnector exists to integrate with external system
- They serve different architectural purposes

### Critical Success Factors:
✅ Defensive idempotency enforcement
✅ Retrieve Parquet from S3 by URI (not from Redis)
✅ Manage complete output lifecycle
✅ Maintain audit trail
✅ Isolate GRID dependency
✅ Handle errors gracefully

---

## REDIS USAGE CLARIFICATION

### What Redis IS used for:
1. **Idempotency keys** (UUID, ~36 bytes)
2. **Correlation IDs** (UUID, ~36 bytes)
3. **S3 URI references** (~100-200 bytes)
4. **Processing metadata** (~500 bytes - 1KB)
5. **State tracking** (pending/processing/complete)

**Total per entry: ~1-2 KB**

### What Redis is NOT used for:
❌ Full Parquet files (tens of MB)
❌ Large binary payloads
❌ Bulk data storage
❌ As a data transport mechanism

### Two-Layer Idempotency:

#### Edge Idempotency (Lambda):
- Prevents duplicate downstream invocations
- Checks BEFORE processing
- Generates and stores key BEFORE calling backend
- Protects against S3 at-least-once delivery

#### Defensive Idempotency (Calibration Service):
- Protects against retries, network failures, replays
- Checks AFTER receiving request
- Different key space from edge
- Backend protection layer

**Both layers are REQUIRED** - they serve different purposes.

---

## DATA FLOW SUMMARY

### Happy Path:

1. **External Trigger**
   - Batch scheduler triggers API Gateway
   - S3 event notification (at-least-once delivery)

2. **API Gateway**
   - JWT validation via ACM
   - Route to XENA Loader Lambda

3. **XENA Loader (Lambda)** - Edge Gateway
   - Generate idempotency key (UUID)
   - Check Redis (abort if duplicate)
   - **STREAM** Parquet from S3 Input (no buffering!)
   - **EARLY** schema validation
   - **REJECT** if invalid (return error)
   - Build reference object (~1KB):
     - S3 URI
     - Idempotency key
     - Metadata
   - Store reference in Redis (fast <100ms)
   - Trigger SCIB Gateway with keys
   - **Total time: <2 seconds**

4. **SCIB Security Gateway (EKS)**
   - JWT verification
   - Route to Calibration Service

5. **Calibration Service (EKS)** - Business Orchestration
   - Extract idempotency key from header
   - Check Redis defensive idempotency (abort if processed)
   - Retrieve reference from Redis by key
   - **Retrieve Parquet from S3 by URI** (source of truth)
   - Prepare calibration request
   - Invoke Grid Connector
   - Wait for GRID results
   - Process & transform results
   - Write calibrated Parquet to S3 Output
   - Update Redis (mark complete, store output URI)
   - Audit trail logging

6. **Grid Connector (EKS)** - External Integration
   - Call MTO GRID (AWS external)
   - QuantLib calibration execution
   - Return results

7. **Output Consumption**
   - S3 Output Bucket contains calibrated Parquet
   - Markets Platforms consume via Sigma/DaaS

### Error Path (Invalid Data):

1. XENA Loader streams from S3
2. **Schema validation FAILS**
3. **Error Handler** logs failure
4. Return HTTP 400 error to caller
5. **NO Redis pollution**
6. **NO downstream processing**
7. **NO EKS resource consumption**

**This is the fail-fast principle in action.**

---

## PERFORMANCE CHARACTERISTICS

### Current (Problematic) Implementation:
- Lambda execution: ~30+ seconds
  - S3 client + Redis init: ~22s
  - S3 object read (buffered): ~5.1s
  - Redis save (full object): ~5.2s
- **This is NOT acceptable**

### Target (Correct) Implementation:

#### Lambda (XENA Loader):
- Cold start: ~1s
- Streaming setup: ~500ms
- Schema validation: ~500ms
- Redis write (reference): <100ms
- Downstream trigger: ~200ms
- **TOTAL: <2 seconds typical**

#### Calibration Service:
- Defensive idempotency check: <50ms
- S3 Parquet retrieval: ~1-3s (depends on size)
- GRID invocation: ~variable (external dependency)
- Output write: ~1-2s
- **Total: Dominated by GRID execution time**

### Scalability:
- **Streaming architecture** scales linearly with file size
- **No memory pressure** from large files
- **Redis performance** independent of Parquet size
- **EKS resources** protected by early validation

---

## FUTURE ENHANCEMENTS (Deferred from MVP1)

### SQS Integration (Planned for MVP2+):
- Natural back-pressure handling
- Decouple ingestion from processing
- Retry semantics without tight coupling
- Dead-letter queues for failure isolation
- Improved observability & replayability

**Note:** SQS was included in Security Architecture (CISO Assessment & IriusRisk) for MVP1 but implementation deferred due to time constraints.

---

## KEY TAKEAWAYS

### ✅ DO:
1. Stream Parquet from S3 (use ResponseInputStream)
2. Validate schema EARLY in Lambda (fail-fast)
3. Store S3 URI references in Redis (not full files)
4. Enforce edge idempotency in Lambda
5. Enforce defensive idempotency in backend
6. Use S3 as source of truth
7. Separate CalibrationService from GRIDConnector concerns
8. Target <2 second Lambda execution
9. Maintain audit trails for compliance
10. Protect EKS resources with early validation

### ❌ DON'T:
1. Buffer full Parquet files in memory
2. Store large payloads in Redis
3. Validate schema late in the pipeline
4. Let invalid data reach EKS
5. Use Redis as bulk data transport
6. Assume CalibrationService is just a pass-through
7. Ignore performance targets
8. Skip idempotency checks
9. Allow 30+ second Lambda executions
10. Pollute Redis with invalid data

---

## CONCLUSION

The XENA MVP1 architecture is designed around three core principles:

1. **Early Validation** - Reject invalid data at the edge
2. **Streaming Architecture** - No memory buffering
3. **Clear Separation of Concerns** - Each component has one job

The current performance issues (especially for files >20 MB) are **expected outcomes** of architectural drift from these principles. Returning to the correct design will:

- ✅ Eliminate 30+ second Lambda executions
- ✅ Reduce Redis memory pressure
- ✅ Protect EKS resources
- ✅ Enable linear scaling with file size
- ✅ Improve operational debugging
- ✅ Meet performance SLAs

**The architecture described in this document is the APPROVED design.** Any deviations must go through formal architectural review.

---

## REFERENCES

### Original Architecture Diagram
See Image 2 (architecture diagram showing correct flow)

### Performance Analysis
See executive summary document identifying deviations

### Idempotency & Parquet Documentation
See Section 4 documentation on Redis usage and file format

### Security Architecture
CISO Assessment & IriusRisk documents (includes SQS for future)

---

**Document Version:** 1.0  
**Date:** 2026-02-06  
**Status:** Authoritative - Correct XENA MVP1 Architecture  
**Replaces:** Any contradictory implementation documentation
