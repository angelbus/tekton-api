# XENA MVP2
## Full Market-To-Market (FMTM)
### Event-Driven Distributed Processing Architecture

**Shared Storage, High-Throughput, Deal-Level Execution**

---

**Architecture Documentation**  
Version 2.0 | March 8, 2026  
**Confidential**

---

# Table of Contents

1. [Executive Summary](#executive-summary)
2. [Document Purpose and Scope](#document-purpose-and-scope)
3. [Architecture Overview](#architecture-overview)
4. [The Shared Storage Model](#the-shared-storage-model)
5. [Component Specifications](#component-specifications)
6. [Cross-Account Security Model](#cross-account-security-model)
7. [S3 Storage Layout and Contracts](#s3-storage-layout-and-contracts)
8. [XDE C++ Processing Model](#xde-cpp-processing-model)
9. [Manifest Completion Contract](#manifest-completion-contract)
10. [Event-Driven Architecture](#event-driven-architecture)
11. [Book Lifecycle State Machine](#book-lifecycle-state-machine)
12. [Failure and Retry Semantics](#failure-and-retry-semantics)
13. [Reconciliation Mechanism](#reconciliation-mechanism)
14. [Performance Characteristics](#performance-characteristics)
15. [Progress Tracking](#progress-tracking)
16. [Comparison: MVP1 vs MVP2](#comparison-mvp1-vs-mvp2)
17. [Deployment Architecture](#deployment-architecture)
18. [Monitoring and Observability](#monitoring-and-observability)
19. [Reliability Guarantees](#reliability-guarantees)

---

# Executive Summary

XENA MVP2 represents a **fundamental architectural transformation** from MVP1, introducing a **deal-level distributed processing model** with **event-driven orchestration** and a **shared S3 storage paradigm** that eliminates polling, payload relay, and memory pressure while achieving **833+ books per minute sustained throughput**.

## Dual Execution Modes

XENA supports two execution modes: **standard processing** and **reconciliation processing**. Reconciliation jobs reuse existing deal results stored in S3 when available, minimizing recomputation and enabling fast book recovery. This stateless idempotency check allows XENA to efficiently recover from failures without requiring external state management.

## Critical Paradigm Shift

**From:** Book-level execution with Redis polling (BLPOP anti-pattern)

**To:** Deal-level parallel execution with S3 shared storage and S3 event-driven completion triggered only by manifest.json creation (suffix filtering)

## Key Achievements

- ✅ **~95% cluster utilization** (up from ~60% in book-level model)
- ✅ **Eliminated polling** - No BLPOP, no memory pressure
- ✅ **Eliminated payload relay** - GRID writes directly to S3
- ✅ **8.3x throughput improvement** - From 100 to 833+ books/min
- ✅ **Reduced tail latency** - Incremental deal-level processing
- ✅ **Better fault tolerance** - Deal-level failure isolation
- ✅ **90% Redis footprint reduction** - Metadata only, no payloads
- ✅ **Manifest-driven completion detection** using S3 suffix-filtered events
- ✅ **Stateless reconciliation** - Idempotent S3-based result checking

## GRID Team Endorsement

The GRID leadership team has **enthusiastically endorsed** this shared storage model, recognizing it will:

- Eliminate current issues with other client applications (e.g., Murex)
- Require minimal GRID changes with low complexity
- Establish a scalable pattern for all future GRID clients

As stated in the official meeting minutes:

> *"Sergio and his team understood the benefits very well: performance improvement, workflow simplification, event-driven architecture, and cost reduction."*

> *"The presentation message is that current GRID clients don't currently show all the potential offered (HTC), as they only focus on Batch processes that don't require High-Throughput (e.g., Murex). This changes starting with XENA MVP2, which also needs to change the integration approach."*

---

# Document Purpose and Scope

This document provides the **authoritative technical specification** for XENA MVP2's Full Market-To-Market (FMTM) architecture, including:

- Complete system architecture and component specifications
- Deal-level distributed processing model
- Shared S3 storage contract and event-driven patterns
- Cross-account security model
- XDE C++ processing engine integration
- Completion manifest contract and reconciliation mechanisms
- Performance characteristics and reliability guarantees
- Progress tracking and observability patterns

This document describes the interaction between **XENA services**, **GRID infrastructure**, and the **XDE compute engine** responsible for deal-level distributed execution.

## Target Audience

- XENA Development Team
- GRID Engineering Team
- Platform Architecture Review Board
- Operations and SRE Teams
- Business Stakeholders

## Component Definitions

- **GRIDConnector**: XENA EKS component responsible for GRID job submission and S3 path communication
- **XDE (eXtended Data Engine)**: C++ execution engine running in GRID containers for parallel deal processing
- **XENALoader Lambda**: Event-triggered function processing manifest.json completion signals
- **EventBridge**: AWS orchestration service for periodic reconciliation checks

---

# 1. Architecture Overview

## 1.1 Component Architecture

XENA MVP2 consists of the following architectural components:

```
XENA Services (EKS)
 ├─ CalibrationService
 ├─ FMTMService
 ├─ GRIDConnector
 └─ Result Processor (SQS + Lambda)

GRID (External AWS Account)
 └─ XDE Execution Engine (C++ container)

AWS Infrastructure
 ├─ S3 Shared Storage
 ├─ EventBridge (Reconciliation Scheduler)
 ├─ SQS (Input/Output Queues)
 └─ Lambda (XENALoader, XENAFilter)
```

## 1.2 Separation of Responsibilities

| Component | Responsibility |
|-----------|----------------|
| **FMTMService** | Book orchestration, manifest processing |
| **CalibrationService** | Calibration orchestration |
| **GRIDConnector** | GRID submission, S3 path communication |
| **XDE** | Deal execution, parallel processing |
| **XENALoader** | Manifest completion trigger |
| **Result Lambda** | Post-processing, validation |

**Critical Principle:** CalibrationService and FMTMService are separated to maintain **Single Responsibility Principle (SRP)**. Each service orchestrates a distinct workflow with independent lifecycle and scaling characteristics.

## 1.3 Strategic Decision: Deal as Unit of Work

The cornerstone architectural decision of XENA MVP2 is the **unit of work** selection for FMTM processing.

### Comparison: Book vs Deal Execution Model

| Model | Cluster Utilization | Latency | Reliability |
|-------|-------------------|---------|-------------|
| Book as Unit of Work | ~60% | High | Poor |
| **Deal as Unit of Work** | **~95%** | **Low** | **Excellent** |

### Conclusion

**FMTM uses: Deal = unit of work**

### Benefits

- **Maximum cluster parallelism** - All GRID workers can process deals simultaneously
- **Better fault tolerance** - Individual deal failures don't fail entire books
- **Incremental results** - Deals complete and are written continuously
- **Reduced tail latency** - Streaming execution eliminates wait for entire book

**Important:** *The Book remains the logical completion boundary, but execution happens at the deal level.*

## 1.4 Architectural Layers

XENA MVP2 is structured around **five distinct architectural planes**:

### 1. Data Plane - S3 Storage

All heavy payloads (Parquet books, deal results) stored exclusively in S3. **S3 is the single source of truth** and the storage contract between all system actors.

- Input: Raw Murex books (Parquet)
- Output: Calibrated deal results (Parquet)
- Error: Failed deal records (JSON)
- No payload duplication
- Direct producer → S3 → consumer flows

### 2. Control Plane - Redis Metadata

Redis stores **only control metadata**:
- Book registration
- Idempotency keys
- Processing state
- Deal coordination
- Typical payload: ~2 KB per book

**NO payload storage. NO BLPOP. NO large objects.**

### 3. Compute Plane - GRID Parallel Processing

MTO GRID XDE C++ engine:
- Reads books **directly from S3** (streaming)
- Processes deals in parallel (multi-threaded)
- Writes results **directly to S3** (no intermediate hops)
- Cross-account IAM role access
- No payload relay through services

### 4. Event Plane - SQS Decoupling

S3 ObjectCreated events trigger SQS queues:
- Decouples ingestion from processing
- Backpressure control
- At-least-once delivery semantics
- DLQ support for failures
- Massive parallelism via Lambda

### 5. Filter Plane - Lambda Processing

Output validation via stateless Lambda:
- Error detection
- Result classification
- Lightweight metadata validation
- Highly parallelizable (200+ concurrent)

## 1.5 High-Level Data Flow

### Ingestion Path

```
Murex/DataLake
    ↓ (PUT Book)
S3 Input Bucket
    ↓ (ObjectCreated Event)
SQS Input Queue
    ↓ (Trigger)
XENALoader (Lambda)
    ↓ (Register metadata + HTTPS)
API Gateway
    ↓
SEPIA (NGINX Ingress)
    ↓
FMTMService (EKS Pod - XENA)
    ↓ (Delegate compute)
GRIDConnector (EKS Pod - XENA)
    ↓ (Start job - pass S3 paths)
MTO GRID (C++ Engine - External AWS)
```

### Processing Path (GRID Direct S3 Access)

```
MTO GRID
    ↓ s3:GetObject (Cross-Account IAM)
S3 Input Bucket (read Book.parquet - STREAMING)
    ↓
XDE C++ Engine
    ↓ Parse deals → Process in parallel
    ↓ s3:PutObject (Cross-Account IAM)
S3 Output Bucket (write deal results + _progress.json + manifest.json)
```

### Output Path

```
S3 Output Bucket
    ↓ (ObjectCreated Event - suffix filter: manifest.json)
SQS Output Queue
    ↓ (Trigger)
XENAFilter (Lambda)
    ↓ (Validate manifest)
    ├─ Valid → Update Redis, notify completion
    └─ Invalid → Alert operations
```

## 1.6 System Context Diagram

```
┌─────────────────┐         ┌──────────────────────────────┐         ┌─────────────────┐
│  Data Lake /    │         │     AWS Account - XENA       │         │ AWS Account -   │
│  Murex          │────────▶│  Event-Driven Orchestration  │◀───────▶│ GRID Compute    │
│  (Producer)     │  Books  │  + Shared Storage            │ Control │ (External)      │
└─────────────────┘         └──────────────────────────────┘         └─────────────────┘
                                        ▲                                      │
                                        │                                      │
                                        └──────── S3 Data Plane ──────────────┘
                                           (Direct Read/Write)
```

---

# 2. The Shared Storage Model

## 2.1 The Paradigm Shift

### Old Pattern (MVP1 - Anti-Pattern)

```
GRID → Redis (payload storage) → BLPOP (polling) → XENA → Download payload
```

**Problems:**
- Memory pressure in Pods
- Payload duplication over network
- Polling inefficiency (constant BLPOP calls)
- Redis misused as data bus
- Poor throughput scaling

### New Pattern (MVP2 - Correct Architecture)

```
GRID → S3 (direct write) → S3 Event (suffix filter: manifest.json) → SQS → Lambda → XENA reacts
```

**Benefits:**
- ✅ **Zero polling** - Event-driven push model
- ✅ **Zero payload duplication** - Single write to S3
- ✅ **Suffix filtering** - Events only for manifest.json, not intermediate deal files
- ✅ **Atomic completion signal** - manifest.json written last
- ✅ **No Lambda overhead** - Filtering at S3 event configuration level
- ✅ **Scalable** - S3 handles any write volume
- ✅ **Durable** - 99.999999999% S3 durability

**Critical:** Filtering at the **S3 event configuration** avoids unnecessary Lambda invocations when intermediate deal files are written.

## 2.2 Three Core Files in XDE Processing

XDE produces three key files during job execution:

### 1. `_progress.json`

**Purpose:** Periodic checkpoint for observability and reconciliation recovery

**Update Frequency:** Every 60 seconds (configurable)

**Contents:**
```json
{
  "jobId": "grid-742912",
  "bookId": "eFX",
  "processedDeals": 52300,
  "totalDeals": 80000,
  "completedDeals": 51500,
  "failedDeals": 800,
  "lastProcessingDeal": "deal-918273",
  "processingRateDealsPerSec": 145,
  "estimatedCompletionSeconds": 210,
  "bookRetries": 1,
  "timestamp": "2026-03-05T12:30:00Z",
  "reconciliation": true
}
```

**Use Cases:**
- Monitoring GRID progress from XENA
- Detecting stalled jobs
- Reconciliation recovery
- ETA calculation

### 2. Deal Results (`deals/deal_*.parquet` or `deals/deal_*.json`)

**Purpose:** Individual deal processing results

**Format:**
- Success: `deal_{dealId}.parquet` (in deals/ directory)
- Failure: `deal_{dealId}.json` (error details)

**Written:** Continuously as deals complete

### 3. `manifest.json`

**Purpose:** **Atomic completion signal** for the entire job

**Written:** LAST, only when all deals complete

**Contents:**
```json
{
  "bookId": "eFX",
  "inputFile": "s3://xena-input/eFX/book.parquet",
  "totalDeals": 48178,
  "completedDeals": 47000,
  "failedDeals": 1178,
  "status": "COMPLETED",
  "startTime": "2026-03-05T18:00:12Z",
  "endTime": "2026-03-05T18:07:42Z",
  "durationSeconds": 450,
  "resultsPath": "s3://xena-output/eFX/deals/",
  "errorPath": "s3://xena-error/eFX/",
  "gridJobId": "grid-742912",
  "version": "1.0"
}
```

**Completion Contract:** `manifest.json` is written **last** and represents the **atomic completion signal** for the job. Its presence triggers the S3 event that notifies XENA of job completion.

## 2.3 S3 Storage Layout Example

```
s3://xena-output/{bookId}/
├── payload.json          # Initial job configuration
├── _progress.json        # Periodic checkpoint (updated every 60s)
├── deals/
│   ├── deal_0001.parquet # Successful deal results
│   ├── deal_0002.parquet
│   ├── deal_0003.parquet
│   └── ...
└── manifest.json         # FINAL completion signal (written LAST)
```

## 2.4 GRID Direct S3 Access Pattern

**Key Principle:** GRID reads input and writes output **directly to/from S3** with no intermediate services.

### Cross-Account S3 Access

```
GRID Account (External)
    ↓ AssumeRole (ExternalId: XENA-GRID-2026)
IAM Role: GridExecutionRole (XENA Account)
    ↓ Permissions:
        - s3:GetObject on xena-input/*
        - s3:PutObject on xena-output/*
        - s3:PutObject on xena-error/*
    ↓
S3 Buckets (XENA Account)
```

### Benefits of Direct Access

- **Performance** - No network hops through services
- **Simplicity** - Clean S3 API contract
- **Security** - IAM-based, auditable with CloudTrail
- **Scalability** - S3 handles any throughput
- **Cost** - No data transfer between services

---

# 3. Component Specifications

## 3.1 FMTMService

### Responsibility

Book orchestration for FMTM processing.

### Key Functions

- Register book processing in Redis
- Send GRID job via GRIDConnector
- Include execution payload with reconciliation parameters
- Handle manifest completion events
- Update book state

### Execution Payload Example

**Standard Processing:**
```json
{
  "jobId": "uuid-12345",
  "bookId": "eFX",
  "valuationDate": "2026-03-05",
  "inputPrefix": "s3://xena-input/eFX/",
  "outputPrefix": "s3://xena-output/eFX/",
  "errorPrefix": "s3://xena-error/eFX/",
  "reconciliation": false,
  "bookRetries": 0,
  "etaSeconds": 180,
  "version": "1.0"
}
```

**Reconciliation Processing:**
```json
{
  "jobId": "uuid-12345",
  "bookId": "eFX",
  "valuationDate": "2026-03-05",
  "inputPrefix": "s3://xena-input/eFX/",
  "outputPrefix": "s3://xena-output/eFX/",
  "errorPrefix": "s3://xena-error/eFX/",
  "reconciliation": true,
  "bookRetries": 1,
  "etaSeconds": 60,
  "version": "1.0"
}
```

### Reconciliation Flag Behavior

When `reconciliation: true`:
- XDE will check S3 for existing deal results before processing
- Skip deals that already have output files in S3
- Only reprocess failed or missing deals
- `bookRetries` counter incremented

**Use Case:** Fast recovery from partial failures without full recomputation.

### Scaling Configuration

- **Replicas:** 3 (production)
- **CPU Request:** 500m
- **CPU Limit:** 2000m
- **Memory Request:** 1Gi
- **Memory Limit:** 4Gi
- **HPA:** Target 70% CPU, min 3, max 10 replicas

## 3.2 CalibrationService

### Responsibility

Calibration orchestration workflow.

### Separation Rationale

CalibrationService is separated from FMTMService to maintain **Single Responsibility Principle (SRP)**. Each service orchestrates a distinct workflow with independent:
- Lifecycle management
- Scaling characteristics
- Error handling
- Performance profiles
- Business logic

### Key Functions

- External API for calibration requests
- Book submission
- GRID job invocation via GRIDConnector
- Calibration-specific validation

**Important:** CalibrationService must **NOT** contain GRID execution logic. Separation is clean.

### Scaling Configuration

- **Replicas:** 2 (production)
- **CPU Request:** 500m
- **CPU Limit:** 1500m
- **Memory Request:** 512Mi
- **Memory Limit:** 2Gi
- **HPA:** Target 70% CPU, min 2, max 8 replicas

## 3.3 GRIDConnector

### Responsibility

Technical integration bridge between XENA and GRID.

### Key Functions

1. **Construct GRID job payload**
2. **Upload initial payload to S3**
3. **Launch XDE container in GRID**
4. **Return acknowledgment to FMTMService**

### Flow

```
XENA Service (FMTMService/CalibrationService)
    ↓ (BookJob payload)
GRIDConnector
    ↓ (Upload payload.json to S3)
S3 Output Bucket
    ↓ (Submit job with S3 paths)
GRID Job Submission API
    ↓ (Launch XDE container)
XDE Execution in GRID
```

### GRID Job Submission Payload

**Example sent to GRID:**
```json
{
  "bookId": "eFX",
  "inputFile": "s3://xena-input/eFX/book.parquet",
  "inputPrefix": "s3://xena-input/eFX/",
  "outputPrefix": "s3://xena-output/eFX/",
  "errorPrefix": "s3://xena-error/eFX/",
  "gridJobId": "grid-742912",
  "reconciliation": false,
  "bookRetries": 0,
  "version": "1.0"
}
```

### Critical Constraints

- **NO payload relay** - Only S3 paths passed to GRID
- **NO data buffering** - Stateless connector
- **Synchronous ACK** - Returns immediately after job submission
- **No compute** - Pure orchestration bridge

### Scaling Configuration

- **Replicas:** 2 (production)
- **CPU Request:** 200m
- **CPU Limit:** 500m
- **Memory Request:** 256Mi
- **Memory Limit:** 512Mi
- **HPA:** Target 70% CPU, min 2, max 5 replicas

## 3.4 XENALoader Lambda

### Responsibility

Process S3 events and trigger XENA orchestration for completed books.

### Critical Architecture Change: S3 Event Suffix Filtering

**IMPORTANT:** The XENALoader Lambda is **only** triggered by `manifest.json` creation, not by intermediate deal files.

### S3 Event Configuration

```yaml
S3 Bucket: xena-output
Event Notification:
  Events:
    - s3:ObjectCreated:Put
    - s3:ObjectCreated:Post
  Filter:
    Suffix: manifest.json  # CRITICAL: Only manifest triggers Lambda
  Destination:
    Type: SQS
    Queue: xena-output-queue
```

### Why Suffix Filtering at S3 Level?

**Filtering at the S3 event configuration avoids unnecessary Lambda invocations when intermediate deal files are written.**

**Incorrect Approach (MVP1 Anti-Pattern):**
```
S3 Event (all files) → Lambda → Filter in code → Discard non-manifest
```
**Problem:** Lambda invoked thousands of times for deal files, then discards them.

**Correct Approach (MVP2):**
```
S3 Event (suffix: manifest.json) → Lambda → Process manifest
```
**Benefit:** Lambda only invoked once per book, when manifest.json is written.

### Lambda Function Logic

```python
def lambda_handler(event, context):
    for record in event['Records']:
        # Extract S3 key
        s3_key = record['s3']['object']['key']
        
        # At this point, we KNOW it's manifest.json due to S3 filter
        # No need to check file suffix in code
        
        bookId = extract_book_id(s3_key)
        
        # Download and parse manifest
        manifest = download_manifest(s3_key)
        
        # Forward to Result Processor via SQS
        send_to_result_queue(bookId, manifest)
        
        # Update Redis state
        update_book_status(bookId, "COMPLETED")
```

### Scaling Configuration

- **Memory:** 512 MB
- **Timeout:** 30 seconds
- **Concurrency:** 200 (reserved)
- **Retry:** 3 attempts
- **DLQ:** Enabled

### Invocation Rate

**With Suffix Filtering:**
- 833 books/min = ~14 invocations/second
- Total Lambda invocations = 1 per book

**Without Suffix Filtering (Anti-Pattern):**
- If average book = 10,000 deals
- 833 books/min × 10,000 deals = 8.33 million deal files/min
- Lambda invoked 8.33 million times/min
- 99.99% of invocations discarded in code

**Savings:** 99.99% reduction in Lambda invocations via S3 suffix filtering.

## 3.5 Result Processing Lambda

### Responsibility

Reduce memory pressure on EKS pods by processing results asynchronously.

### Purpose

- **Decouple result processing from orchestration**
- **Reduce EKS memory footprint**
- **Enable parallel result validation**
- **Maintain separation of concerns**

### Flow

```
XENALoader Lambda
    ↓ (Send manifest + bookId)
SQS Result Queue
    ↓ (Trigger)
Result Processor Lambda
    ↓ (Validate, transform, store)
Result Storage / Downstream Systems
```

### Key Functions

- Validate manifest completeness
- Check deal counts (completedDeals + failedDeals == totalDeals)
- Extract metadata for reporting
- Send notifications to downstream systems
- Archive results for compliance

### Scaling Configuration

- **Memory:** 1024 MB
- **Timeout:** 60 seconds
- **Batch Size:** 10 messages
- **Concurrency:** 100 (reserved)
- **Retry:** 3 attempts
- **DLQ:** Enabled

## 3.6 XDE Execution Engine

### Overview

XDE (eXtended Data Engine) is MTO's optimized C++ execution engine running in **GRID containers** (not XENA EKS). It implements the deal-level parallel processing model with streaming, dynamic batching, and reconciliation support.

### Thread Pool Architecture

XDE uses **three distinct thread pools**:

#### 1. Worker Thread Pool (CPU-Bound)
- **Count:** `2 × CPU cores`
- **Purpose:** Parallel deal processing
- **Tasks:**
  - Pop `DealBatch` from queue
  - Process each deal (compute FMTM)
  - Apply retry logic with exponential backoff
  - Push `Result` to result queue

#### 2. Aggregator Thread (Single Thread)
- **Count:** 1 (coordination thread)
- **Purpose:** Track completion, write progress
- **Tasks:**
  - Pop `Result` from result queue
  - Update atomic counters (processedDeals, completedDeals, failedDeals)
  - Update `lastProcessingDeal` tracker
  - Push to write queue
  - **Progress Writing:** Every 10 minutes OR every 500 deals
  - **Completion Check:** When `processedDeals == totalDeals`, trigger manifest

#### 3. S3 Writer Thread Pool (IO-Bound)
- **Count:** 4 threads
- **Purpose:** Parallel S3 uploads
- **Tasks:**
  - Pop `Result` from write queue
  - If success → upload to Output bucket (`deal_{id}.parquet`)
  - If failed → upload to Error bucket (`deal_{id}.json`)

### Example Thread Configuration (8-core machine)

```
Worker threads:    16 (2 × 8 cores)
Aggregator thread: 1
S3 writer threads: 4
─────────────────────
Total threads:     21
```

### Reconciliation Logic

When `reconciliation: true` in the job payload:

```cpp
if (job.reconciliation) {
    job.bookRetries++;  // Increment retry counter
    
    // For each deal in the book
    for (auto& deal : book.deals) {
        // Check if result already exists in S3
        if (resultExistsInS3(deal.dealId)) {
            skip_deal(deal);  // Skip processing
            continue;
        }
        
        // Process only missing/failed deals
        process_deal(deal);
    }
}
```

**Method:** `resultExistsInS3(dealId)`
- Checks S3 Output bucket for `deals/deal_{dealId}.parquet`
- HEAD request (lightweight, no download)
- Returns true if file exists, false otherwise

**Stateless:** No Redis or external state required for idempotency check.

### Processing Pipeline

```
1. Submit job (receive payload from GRIDConnector)
2. Load payload from S3
3. Decompose book into deals (streaming Parquet reader)
4. Enqueue deals to worker queue (dynamic batching)
5. Process deals in worker threads (parallel execution)
6. Aggregate results (single aggregator thread)
7. Periodically write _progress.json (every 60s or 500 deals)
8. Write manifest.json on completion (atomic signal)
```

### Dynamic Batch Sizing

To achieve **99% cluster utilization**, XDE uses adaptive batch sizing:

```cpp
size_t computeBatchSize() {
    uint64_t remaining = totalDeals - processedDeals;
    
    if (remaining > 20000) return 200;  // Large batches for bulk
    if (remaining > 5000)  return 50;   // Medium batches
    if (remaining > 1000)  return 10;   // Small batches
    
    return 1;  // Individual deals for tail
}
```

**Goal:** Avoid tail latency by reducing batch size as work completes.

### Retry Logic with Exponential Backoff

**Configuration:**
- `maxRetries = 3`
- `baseDelayMs = 100`

**Backoff Formula:**
```cpp
retryDelay = baseDelayMs × 2^attempt
```

**Example Delays:**
```
retry 1 → 100ms
retry 2 → 200ms
retry 3 → 400ms
```

**Pseudo-Code:**
```cpp
Result processDealWithRetry(Deal& deal) {
    int retries = 0;
    
    while (true) {
        try {
            return computeFMTM(deal);  // Attempt processing
        } catch (...) {
            if (retries >= maxRetries) {
                // Mark as failed, write to error bucket
                return Result{deal.dealId, false};
            }
            
            int delay = baseDelayMs * pow(2, retries);
            sleep(delay);
            retries++;
        }
    }
}
```

### Deal-Level Retry Counter

Each deal tracks its own retry attempts:
- `dealRetryCount` - Number of retries for this specific deal
- Independent from `bookRetries` (book-level reconciliation counter)
- Logged in error output for debugging

### Progress Writing Logic

```cpp
bool shouldWriteProgress() {
    auto now = std::chrono::system_clock::now();
    auto elapsed = now - lastProgressWrite;
    
    // Max interval: 10 minutes
    if (elapsed > std::chrono::minutes(10)) {
        return true;
    }
    
    // Every 500 deals
    if (processedDeals % 500 == 0) {
        return true;
    }
    
    return false;
}
```

**Configurable Parameter:**
- `progressWriteInterval = 10 minutes` (configurable via payload)

---

# 4. Cross-Account Security Model

## 4.1 IAM Role Assumption

GRID jobs running in the **GRID AWS Account** assume an IAM role in the **XENA AWS Account** to access S3 buckets.

### Trust Relationship

**Role Name:** `GridExecutionRole`

**Account:** XENA AWS Account

**Trust Policy:**
```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "AWS": "arn:aws:iam::GRID-ACCOUNT-ID:root"
      },
      "Action": "sts:AssumeRole",
      "Condition": {
        "StringEquals": {
          "sts:ExternalId": "XENA-GRID-2026"
        }
      }
    }
  ]
}
```

### Permission Policy

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "s3:GetObject"
      ],
      "Resource": [
        "arn:aws:s3:::xena-input-prod/*"
      ]
    },
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject"
      ],
      "Resource": [
        "arn:aws:s3:::xena-output-prod/*",
        "arn:aws:s3:::xena-error-prod/*"
      ]
    }
  ]
}
```

### Security Benefits

- **Least Privilege:** GRID can only read from Input, write to Output/Error
- **ExternalId Protection:** Prevents confused deputy attacks
- **CloudTrail Auditability:** All S3 access logged with role assumption
- **Centralized Governance:** Permissions managed in XENA account
- **No Credential Sharing:** No long-lived access keys
- **Revocable:** Can revoke role at any time

## 4.2 S3 Bucket Policies

Additional bucket-level protection:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "AWS": "arn:aws:iam::XENA-ACCOUNT-ID:role/GridExecutionRole"
      },
      "Action": "s3:PutObject",
      "Resource": "arn:aws:s3:::xena-output-prod/*",
      "Condition": {
        "StringEquals": {
          "s3:x-amz-server-side-encryption": "AES256"
        }
      }
    }
  ]
}
```

**Enforces:** Server-side encryption for all objects written by GRID.

---

# 5. S3 Storage Layout and Contracts

## 5.1 Precise Storage Layout

### Input Bucket

```
s3://xena-input-{env}/{bookId}/
└── book.parquet          # Murex book export (streaming read by XDE)
```

### Output Bucket

```
s3://xena-output-{env}/{bookId}/
├── payload.json          # Initial job configuration
├── _progress.json        # Periodic checkpoint (updated every 60s)
├── deals/
│   ├── deal_0001.parquet # Successful deal results
│   ├── deal_0002.parquet
│   ├── deal_0003.parquet
│   └── ...
└── manifest.json         # FINAL completion signal (written LAST)
```

### Error Bucket

```
s3://xena-error-{env}/{bookId}/
└── deals/
    ├── deal_0012.json    # Failed deal with error details
    ├── deal_0045.json
    └── ...
```

## 5.2 File Purpose Summary

| File | Purpose | Update Frequency | Required for Completion |
|------|---------|------------------|------------------------|
| `payload.json` | Job configuration | Once (at start) | No |
| `_progress.json` | Periodic checkpoint | Every 60s or 500 deals | No |
| `deals/*.parquet` | Individual results | Continuous (as deals complete) | No |
| `manifest.json` | **Atomic completion signal** | **Once (LAST)** | **YES** |

## 5.3 Storage Contracts

### Contract 1: Streaming Input

**XDE must NOT load the entire book into memory.**

- Use Apache Arrow C++ Parquet streaming reader
- Read in chunks (configurable chunk size)
- Bounded memory usage
- `hasNext()` / `nextDeal()` iterator pattern

### Contract 2: Incremental Output

**XDE writes deal results as they complete, not in batch.**

- Each worker thread writes independently
- No aggregation in memory before S3 write
- S3 becomes the aggregation layer
- Enables progress monitoring

### Contract 3: Manifest Last

**`manifest.json` MUST be written LAST.**

- Only after `completedDeals + failedDeals == totalDeals`
- Atomic completion signal
- Triggers S3 event → SQS → Lambda → XENA notification
- No partial manifests allowed

### Contract 4: Progress Checkpoints

**`_progress.json` updated periodically for observability.**

- Max interval: 10 minutes (configurable)
- Or every 500 deals (configurable)
- Enables reconciliation detection
- Provides ETA calculation

---

# 6. XDE C++ Processing Model

## 6.1 Processing Pipeline

### 1. Submit Job

**Trigger:** GRIDConnector calls GRID API with job payload

**Payload Example:**
```json
{
  "bookId": "eFX",
  "inputFile": "s3://xena-input/eFX/book.parquet",
  "outputPrefix": "s3://xena-output/eFX/",
  "errorPrefix": "s3://xena-error/eFX/",
  "reconciliation": false,
  "bookRetries": 0
}
```

### 2. Load Payload

**Action:** XDE downloads `payload.json` from S3

**Initialization:**
- Parse JSON
- Extract S3 paths
- Set reconciliation mode
- Initialize atomic counters

### 3. Decompose Book into Deals

**Streaming Parquet Reader:**
```cpp
ParquetReader reader(job.inputFile);

while (reader.hasNext()) {
    Deal deal = reader.nextDeal();
    dealQueue.push(deal);
    totalDeals++;
}
```

**Critical:** Never materialize entire book in memory.

### 4. Enqueue Deals (Dynamic Batching)

**Batch Optimization:**
```cpp
DealBatch batch;
size_t batchSize = computeBatchSize();  // Adaptive sizing

for (size_t i = 0; i < batchSize && reader.hasNext(); i++) {
    Deal deal = reader.nextDeal();
    
    // Reconciliation check
    if (job.reconciliation && resultExistsInS3(deal.dealId)) {
        continue;  // Skip already-processed deals
    }
    
    batch.deals.push_back(deal);
}

dealQueue.push(std::move(batch));
```

### 5. Process Deals in Worker Threads

**Worker Thread Logic:**
```cpp
void workerThread() {
    DealBatch batch;
    
    while (dealQueue.pop(batch)) {
        for (auto& deal : batch.deals) {
            Result result = processDealWithRetry(deal);
            resultQueue.push(std::move(result));
        }
    }
}
```

**Parallel Execution:**
- 16 worker threads (on 8-core machine)
- Lock-free concurrent queue
- Move semantics (zero-copy)
- Independent processing (no shared state)

### 6. Aggregate Results

**Aggregator Thread Logic:**
```cpp
void aggregatorThread() {
    while (processedDeals < totalDeals) {
        Result result;
        resultQueue.pop(result);
        
        // Update atomic counters
        processedDeals++;
        if (result.success) {
            completedDeals++;
        } else {
            failedDeals++;
        }
        
        lastProcessingDeal = result.dealId;
        
        // Forward to S3 writers
        writeQueue.push(std::move(result));
        
        // Periodic progress write
        if (shouldWriteProgress()) {
            writeProgress();
        }
    }
}
```

### 7. Periodically Write `_progress.json`

**Frequency:** Every 60s OR every 500 deals

**Content:**
```json
{
  "jobId": "grid-742912",
  "bookId": "eFX",
  "inputFile": "s3://xena-input/eFX/book.parquet",
  "totalDeals": 48178,
  "processedDeals": 17842,
  "completedDeals": 17000,
  "failedDeals": 842,
  "lastProcessingDeal": "deal-918273",
  "processingRateDealsPerSec": 145,
  "estimatedCompletionSeconds": 210,
  "startTime": "2026-03-05T18:00:12Z",
  "lastUpdate": "2026-03-05T18:42:11Z",
  "reconciliation": false,
  "bookRetries": 0,
  "gridJobId": "grid-742912",
  "version": "1.0"
}
```

**Purpose:**
- XENA can monitor GRID progress via S3
- Reconciliation can detect stalled jobs
- ETA calculation for dashboards

### 8. Write `manifest.json` on Completion

**Condition:**
```cpp
if (completedDeals + failedDeals == totalDeals) {
    writeManifest();
}
```

**Manifest Content:**
```json
{
  "bookId": "eFX",
  "inputFile": "s3://xena-input/eFX/book.parquet",
  "totalDeals": 48178,
  "completedDeals": 47000,
  "failedDeals": 1178,
  "status": "COMPLETED",
  "startTime": "2026-03-05T18:00:12Z",
  "endTime": "2026-03-05T18:07:42Z",
  "durationSeconds": 450,
  "resultsPath": "s3://xena-output/eFX/deals/",
  "errorPath": "s3://xena-error/eFX/",
  "gridJobId": "grid-742912",
  "version": "1.0"
}
```

**Critical:** This is the **LAST** file written. Its creation triggers the S3 event.

## 6.2 Reconciliation Processing

### Pseudo-Code Behavior

```cpp
if (job.reconciliation) {
    // Increment book retry counter
    job.bookRetries++;
    
    // For each deal in the book
    while (reader.hasNext()) {
        Deal deal = reader.nextDeal();
        
        // Idempotency check: Does result already exist in S3?
        if (resultExistsInS3(deal.dealId)) {
            // Skip processing, reuse existing result
            skip_deal(deal);
            continue;
        }
        
        // Only process missing/failed deals
        process_deal(deal);
    }
}
```

### S3 Idempotency Check

**Method:** `resultExistsInS3(dealId)`

**Implementation:**
```cpp
bool resultExistsInS3(const std::string& dealId) {
    std::string key = job.outputPrefix + "/deals/deal_" + dealId + ".parquet";
    
    // HEAD request (lightweight, no download)
    auto response = s3Client.HeadObject(bucket, key);
    
    return response.IsSuccess();
}
```

**Performance:**
- HEAD request ~5ms (no data transfer)
- Parallelized across worker threads
- Cached results (in-memory map)

**Benefits:**
- **Stateless** - No Redis or external state
- **Fast** - Lightweight HEAD requests
- **Reliable** - S3 is source of truth
- **Simple** - Standard S3 API

---

# 7. Manifest Completion Contract

## 7.1 Explicit Rule

**`manifest.json` MUST be written LAST.**

## 7.2 Rationale

The manifest serves as the **atomic completion signal** for the entire job. Writing it prematurely would trigger XENA orchestration before all deals are complete, leading to:

- Incomplete result sets
- Incorrect deal counts
- False completion signals
- Data consistency violations

## 7.3 Enforcement

**In XDE Aggregator:**
```cpp
void aggregatorThread() {
    while (processedDeals < totalDeals) {
        // Process results...
    }
    
    // Only after loop completes (all deals processed)
    writeManifest();
}
```

**Atomic Check:**
```cpp
if (completedDeals + failedDeals == totalDeals) {
    writeManifest();
} else {
    log_error("Deal count mismatch - manifest not written");
    throw std::runtime_error("Incomplete processing");
}
```

## 7.4 S3 Event Pipeline

**Completion Flow:**
```
All deals processed
    ↓
Aggregator verifies count
    ↓
Write manifest.json to S3
    ↓
S3 Event (ObjectCreated:Put, suffix: manifest.json)
    ↓
SQS Output Queue
    ↓
XENALoader Lambda
    ↓
Update Redis: status=COMPLETED
    ↓
Notify downstream systems
```

**Critical:** The S3 event is **filtered by suffix** (`manifest.json`) to avoid triggering for intermediate deal files.

---

# 8. Event-Driven Architecture

## 8.1 Corrected Flow Diagram

```
XENA Service (FMTMService)
    ↓ (Orchestrate)
GRIDConnector
    ↓ (Submit job with S3 paths)
GRID
    ↓ (Launch XDE container)
XDE Execution
    ↓ (Stream from S3, process deals)
S3 writes results
    ├─ deals/*.parquet (continuous)
    ├─ _progress.json (every 60s)
    └─ manifest.json (LAST - completion signal)
         ↓
S3 Event Notification (suffix filter: manifest.json)
    ↓
SQS Output Queue
    ↓ (Trigger)
XENALoader Lambda
    ↓ (Process manifest)
    ├─ Update Redis (status=COMPLETED)
    ├─ Remove from pendingBooks
    └─ Send to Result Processor
         ↓
SQS Result Queue
    ↓ (Trigger)
Result Processor Lambda
    ↓ (Validate, notify)
Downstream Systems
```

## 8.2 Event Types and Filtering

### S3 Event Configuration

**Output Bucket Event Notification:**
```yaml
NotificationConfiguration:
  QueueConfigurations:
    - Id: ManifestCompletionEvent
      QueueArn: arn:aws:sqs:REGION:ACCOUNT:xena-output-queue
      Events:
        - s3:ObjectCreated:Put
        - s3:ObjectCreated:Post
      Filter:
        Key:
          FilterRules:
            - Name: suffix
              Value: manifest.json
```

**Why Suffix Filtering?**

| Approach | Lambda Invocations (48K deals/book) | Efficiency |
|----------|-------------------------------------|------------|
| **No Filter** | 48,001 (all files) | 0.002% useful |
| **Lambda Code Filter** | 48,001 (discard 48K in code) | 0.002% useful |
| **S3 Suffix Filter** | 1 (manifest only) | 100% useful |

**Savings:** 99.998% reduction in Lambda invocations.

### SQS Queue Configuration

**xena-output-queue:**
- **Visibility Timeout:** 60 seconds
- **Message Retention:** 14 days
- **Receive Wait Time:** 20 seconds (long polling)
- **Redrive Policy:** After 3 failures → DLQ
- **DLQ:** xena-output-dlq

## 8.3 EventBridge Orchestration

**Reconciliation Scheduler:**
```yaml
EventBridge Rule: XENAReconciliationScheduler
Schedule: rate(10 minutes)
Target: ReconciliationLambda
Input:
  action: check_stalled_books
```

**Purpose:** Detect stalled jobs by checking `_progress.json` last update time.

---

# 9. Book Lifecycle State Machine

## 9.1 State Diagram with Reconciliation

```
SUBMITTED
   ↓
QUEUED (in SQS)
   ↓
PROCESSING (GRID executing)
   ↓
   ├─ Success Path ─────────────────┐
   │                                 ↓
   │                            COMPLETED
   │                                 ↓
   │                            FILTERED (validation passed)
   │
   └─ Failure Path ─────────────────┐
                                     ↓
                                  FAILED
                                     ↓
                              RECONCILIATION
                                     ↓
                              (Retry with reconciliation=true)
                                     ↓
                              PROCESSING (skip existing deals)
                                     ↓
                              COMPLETED
```

## 9.2 State Transitions

| From State | Event | To State | Action |
|------------|-------|----------|--------|
| - | Book uploaded to S3 | SUBMITTED | Create Redis entry |
| SUBMITTED | SQS message picked up | QUEUED | Lambda processing |
| QUEUED | Lambda invokes FMTM | PROCESSING | GRID job started |
| PROCESSING | manifest.json written | COMPLETED | Update Redis |
| COMPLETED | XENAFilter validates | FILTERED | Notify downstream |
| PROCESSING | Stalled (>10 min no progress) | FAILED | Alert operations |
| FAILED | Manual retry triggered | RECONCILIATION | Set reconciliation=true |
| RECONCILIATION | GRID job restarted | PROCESSING | Skip existing deals |

## 9.3 New State: RECONCILIATION

**Purpose:** Represents a book being reprocessed with idempotency checks.

**Characteristics:**
- `reconciliation: true` in payload
- `bookRetries` counter incremented
- XDE skips deals with existing S3 results
- Only failed/missing deals reprocessed

**Transition Logic:**
```python
if book.status == "FAILED":
    book.status = "RECONCILIATION"
    book.reconciliation = True
    book.bookRetries += 1
    submit_to_grid(book)
```

---

# 10. Failure and Retry Semantics

## 10.1 Retry Parameters

### Deal-Level Retry (within XDE)

**Configuration:**
- `maxRetries = 3`
- `backoff = exponential`

**Formula:**
```
retryDelay = baseDelayMs × 2^attempt
```

**Example Sequence:**
```
Attempt 1: Execute immediately
  ↓ Failure
Attempt 2: Wait 100ms, retry
  ↓ Failure
Attempt 3: Wait 200ms, retry
  ↓ Failure
Attempt 4: Wait 400ms, retry
  ↓ Failure
Mark as FAILED, write to error bucket
```

### Book-Level Retry (XENA Orchestration)

**Trigger:** EventBridge detects stalled book

**Action:** Submit reconciliation job

**Parameters:**
- `reconciliation: true`
- `bookRetries++`

## 10.2 Deal-Level Retry Counter

Each deal tracks its retry attempts independently:

**Error Output Example:**
```json
{
  "dealId": "deal-12345",
  "status": "FAILED",
  "error": "Computation timeout",
  "dealRetryCount": 3,
  "lastAttempt": "2026-03-05T18:45:00Z",
  "bookId": "eFX",
  "gridJobId": "grid-742912"
}
```

**Use Cases:**
- Debugging persistent failures
- Identifying problematic deals
- Performance analysis
- Error pattern detection

## 10.3 Failure Isolation

**Critical Principle:** Deal failures do NOT fail the entire book.

**Behavior:**
```cpp
for (auto& deal : batch.deals) {
    try {
        Result result = processDealWithRetry(deal);
        resultQueue.push(result);
    } catch (...) {
        // Catch exception, mark deal as failed
        Result failedResult{deal.dealId, false};
        resultQueue.push(failedResult);
        // Continue processing next deal
    }
}
```

**Outcome:**
- Book completes with partial results
- Manifest shows: `completedDeals=47000, failedDeals=1178`
- Failed deals written to error bucket
- Successful deals available immediately
- Reconciliation can retry only failed deals

---

# 11. Reconciliation Mechanism

## 11.1 Overview

Reconciliation enables **fast recovery from partial failures** by reusing existing deal results stored in S3 when available, minimizing recomputation.

## 11.2 Trigger Conditions

**Manual Trigger:**
- Operations team identifies failed book
- Triggers reconciliation via API

**Automatic Trigger:**
- EventBridge scheduler detects stalled book
- `_progress.json` not updated in >10 minutes
- Automatically triggers reconciliation

## 11.3 Reconciliation Parameters

**Payload Changes:**
```json
{
  "reconciliation": true,  // Enable idempotency checks
  "bookRetries": 1,        // Increment from previous attempt
  "etaSeconds": 60         // Reduced ETA (fewer deals to process)
}
```

## 11.4 XDE Reconciliation Logic

### Pseudo-Code

```cpp
if (job.reconciliation) {
    // Increment book retry counter
    job.bookRetries++;
    
    // Reset counters for this attempt
    processedDeals = 0;
    completedDeals = 0;
    failedDeals = 0;
    
    // Stream book deals
    while (reader.hasNext()) {
        Deal deal = reader.nextDeal();
        
        // IDEMPOTENCY CHECK
        if (resultExistsInS3(deal.dealId)) {
            // Deal already processed successfully
            processedDeals++;
            completedDeals++;  // Count as completed (reused)
            continue;  // Skip processing
        }
        
        // Deal missing or failed - process it
        enqueue_for_processing(deal);
    }
}
```

### S3 Idempotency Check Implementation

```cpp
bool resultExistsInS3(const std::string& dealId) {
    // Construct expected S3 key
    std::string key = job.outputPrefix + "/deals/deal_" + dealId + ".parquet";
    
    // Check if object exists (HEAD request)
    try {
        auto response = s3Client.HeadObject(bucket, key);
        return response.IsSuccess();
    } catch (...) {
        return false;  // Object doesn't exist
    }
}
```

**Optimization:** Cache results in memory map to avoid redundant S3 calls:
```cpp
std::unordered_map<std::string, bool> dealResultCache;

bool resultExistsInS3Cached(const std::string& dealId) {
    auto it = dealResultCache.find(dealId);
    if (it != dealResultCache.end()) {
        return it->second;  // Return cached result
    }
    
    bool exists = resultExistsInS3(dealId);
    dealResultCache[dealId] = exists;
    return exists;
}
```

## 11.5 Reconciliation Benefits

**Performance:**
- If 90% of deals succeeded in first attempt
- Reconciliation only processes 10% of deals
- **10x faster** than full recompute

**Reliability:**
- Automatic recovery from transient failures
- No manual intervention required
- Preserves work already completed

**Cost:**
- Reduced compute time
- Reduced S3 write costs
- Minimal HEAD request overhead

## 11.6 Stateless Design

**Critical:** Reconciliation does NOT require Redis or external state.

**Why Stateless?**
- S3 is the source of truth
- Result existence = deal completed
- No synchronization required
- Simple, reliable, auditable

**Contrast with Stateful Approach:**
```
❌ Stateful (Anti-Pattern):
   - Store processed deal IDs in Redis
   - Check Redis before processing
   - Problems: Redis failure = state loss, synchronization overhead

✅ Stateless (MVP2):
   - Check S3 for result file
   - S3 existence = completion
   - Benefits: Simple, reliable, no external dependencies
```

---

# 12. Performance Characteristics

## 12.1 Target Performance

**Throughput:** 833+ books/min sustained

**Latency:**
- XENA overhead: <10s (P95)
- GRID processing: ~450s for 48K deals (example)
- Total end-to-end: <8 minutes for typical book

**Cluster Utilization:** ~95% (vs ~60% in book-level model)

**Progress Checkpoint Interval:** Every 10 minutes (MAX) or every 500 deals

## 12.2 Throughput Calculation

**Example Book:**
- Total deals: 48,178
- Processing rate: 145 deals/sec (measured)

**Calculation:**
```
Time = 48,178 deals ÷ 145 deals/sec = 332 seconds ≈ 5.5 minutes
```

**XENA Overhead:**
```
Ingestion:        ~2s
Validation:       ~1s
GRID Submission:  ~3s
Manifest Processing: ~2s
Total:            ~8s
```

**End-to-End:**
```
GRID Processing:  332s
XENA Overhead:    8s
Total:            340s ≈ 5.7 minutes
```

**Books per Minute:**
```
60 seconds ÷ 340 seconds = 0.176 books/sec × 60 = 10.6 books/min per GRID worker

With 80 parallel GRID workers:
10.6 books/min × 80 = 848 books/min
```

**Target:** 833 books/min (with safety margin)

## 12.3 Progress Checkpoint Configuration

**Configurable Parameters:**
- `progressWriteInterval`: 10 minutes (default)
- `progressDealThreshold`: 500 deals (default)

**Logic:**
```cpp
bool shouldWriteProgress() {
    auto now = std::chrono::system_clock::now();
    auto elapsed = now - lastProgressWrite;
    
    // Time-based trigger
    if (elapsed > std::chrono::minutes(progressWriteInterval)) {
        return true;
    }
    
    // Deal count trigger
    if (processedDeals % progressDealThreshold == 0) {
        return true;
    }
    
    return false;
}
```

**Purpose:**
- Monitoring: Observe GRID progress from XENA
- Reconciliation: Detect stalled jobs
- Debugging: Track processing rate
- ETA: Estimate completion time

## 12.4 Resource Utilization

### GRID Cluster (Example: 8-core workers)

**Worker Threads:** 16 (2 × 8 cores)
**CPU Utilization:** ~95% (sustained)
**Memory:** Bounded (streaming, no full book load)

**Efficiency Improvement:**
- Book-level: ~60% utilization (waiting for slowest deal)
- Deal-level: ~95% utilization (continuous parallel work)

### XENA EKS

**FMTMService:**
- CPU: <20% (lightweight orchestration)
- Memory: <500 MB per pod

**GRIDConnector:**
- CPU: <10% (stateless bridge)
- Memory: <256 MB per pod

### Lambda

**XENALoader:**
- Invocations: ~14/sec (833 books/min)
- Duration: ~500ms P95
- Memory: ~150 MB peak

**Result Processor:**
- Invocations: ~14/sec
- Duration: ~2s P95
- Memory: ~300 MB peak

---

# 13. Progress Tracking

## 13.1 Overview

The `_progress.json` file provides **real-time observability** into GRID processing, enabling monitoring, reconciliation detection, and ETA calculation.

## 13.2 Progress File Structure

**File:** `s3://xena-output/{bookId}/_progress.json`

**Update Frequency:** Every 60 seconds OR every 500 deals (whichever comes first)

**Schema:**
```json
{
  "jobId": "grid-742912",
  "bookId": "eFX",
  "inputFile": "s3://xena-input/eFX/book.parquet",
  
  "totalDeals": 48178,
  "processedDeals": 17842,
  "completedDeals": 17000,
  "failedDeals": 842,
  
  "lastProcessingDeal": "deal-918273",
  "lastWorkerId": "xde-node-12",
  
  "processingRateDealsPerSec": 145,
  "estimatedCompletionSeconds": 210,
  
  "startTime": "2026-03-05T18:00:12Z",
  "lastUpdate": "2026-03-05T18:42:11Z",
  
  "reconciliation": false,
  "bookRetries": 0,
  
  "gridJobId": "grid-742912",
  "version": "1.0"
}
```

## 13.3 Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `jobId` | string | GRID job identifier |
| `bookId` | string | Book identifier |
| `inputFile` | string | S3 path to input book |
| `totalDeals` | int | Total number of deals in book |
| `processedDeals` | int | Deals processed so far (completed + failed) |
| `completedDeals` | int | Successfully processed deals |
| `failedDeals` | int | Failed deals |
| `lastProcessingDeal` | string | Most recently processed deal ID |
| `lastWorkerId` | string | Worker that processed last deal |
| `processingRateDealsPerSec` | double | Current processing rate |
| `estimatedCompletionSeconds` | int | ETA to completion |
| `startTime` | string | Job start timestamp (ISO 8601) |
| `lastUpdate` | string | Last progress update timestamp |
| `reconciliation` | bool | Whether this is a reconciliation job |
| `bookRetries` | int | Number of book-level retries |
| `gridJobId` | string | GRID job ID |
| `version` | string | Progress file schema version |

## 13.4 Use Cases

### 1. Monitoring GRID Progress

**From XENA:**
```python
# Periodically fetch progress
progress = s3.get_object(
    Bucket="xena-output",
    Key=f"{bookId}/_progress.json"
)

data = json.loads(progress['Body'].read())

# Display in dashboard
print(f"Progress: {data['processedDeals']} / {data['totalDeals']}")
print(f"ETA: {data['estimatedCompletionSeconds']} seconds")
```

### 2. Reconciliation Detection

**EventBridge Lambda:**
```python
def check_stalled_books(event, context):
    # Get all books in PROCESSING state
    pending_books = redis.smembers("pendingBooks")
    
    for book_id in pending_books:
        # Check progress file
        try:
            progress = download_progress(book_id)
            last_update = parse_timestamp(progress['lastUpdate'])
            now = datetime.utcnow()
            
            # If no update in 10+ minutes, mark stalled
            if (now - last_update).seconds > 600:
                mark_stalled(book_id)
                trigger_reconciliation(book_id)
        except:
            # Progress file missing - possible issue
            alert_operations(book_id)
```

### 3. ETA Calculation

**Formula:**
```python
def calculate_eta(progress):
    remaining_deals = progress['totalDeals'] - progress['processedDeals']
    rate = progress['processingRateDealsPerSec']
    
    if rate > 0:
        eta_seconds = remaining_deals / rate
        return eta_seconds
    else:
        return None
```

### 4. Performance Analysis

**Metrics:**
```python
# Average processing rate
avg_rate = progress['processedDeals'] / elapsed_seconds

# Deal throughput per worker
workers = 16  # Example
rate_per_worker = progress['processingRateDealsPerSec'] / workers

# Cluster efficiency
efficiency = (rate_per_worker / max_theoretical_rate) × 100
```

## 13.5 Progress Write Implementation

**XDE Aggregator:**
```cpp
void writeProgress() {
    // Build progress JSON
    Progress p;
    p.bookId = job.bookId;
    p.jobId = job.gridJobId;
    p.totalDeals = totalDeals;
    p.processedDeals = processedDeals;
    p.completedDeals = completedDeals;
    p.failedDeals = failedDeals;
    p.lastProcessingDeal = lastProcessingDeal;
    
    // Calculate metrics
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime
    ).count();
    
    p.processingRateDealsPerSec = (double)processedDeals / elapsed;
    
    uint64_t remaining = totalDeals - processedDeals;
    p.estimatedCompletionSeconds = remaining / p.processingRateDealsPerSec;
    
    p.lastUpdate = format_timestamp(now);
    p.reconciliation = job.reconciliation;
    p.bookRetries = job.bookRetries;
    
    // Write to S3
    std::string json = serialize(p);
    s3.write(job.outputPrefix + "/_progress.json", json);
    
    lastProgressWrite = now;
}
```

## 13.6 Progress File Lifecycle

```
Job Start
    ↓
Initial progress write (processedDeals=0)
    ↓
Periodic updates (every 60s or 500 deals)
    ↓
Final update (processedDeals=totalDeals)
    ↓
manifest.json written (completion signal)
    ↓
Progress file remains (historical record)
```

**Retention:** Progress files retained for 30 days for debugging and audit purposes.

---

# 14. Comparison: MVP1 vs MVP2

## 14.1 Comprehensive Feature Comparison

| Feature | MVP1 | MVP2 |
|---------|------|------|
| **Unit of Work** | Book (monolithic) | Deal (parallel) |
| **Cluster Utilization** | ~60% | ~95% |
| **Throughput** | 100 books/min | 833+ books/min |
| **GRID → XENA Communication** | Redis BLPOP (polling) | S3 Event (push) |
| **Payload Storage** | Redis (memory pressure) | S3 (scalable) |
| **Result Aggregation** | In-memory (bottleneck) | S3 (distributed) |
| **Completion Detection** | Redis polling | S3 manifest event (suffix filtering) |
| **Progress Tracking** | None | _progress.json (every 60s) |
| **Reconciliation** | Full recompute | Partial recompute (stateless) |
| **Failure Isolation** | Book-level (all-or-nothing) | Deal-level (partial success) |
| **Memory Footprint** | Large (full books in Redis) | Small (metadata only) |
| **Redis Usage** | Data bus (anti-pattern) | Metadata only |
| **GRID Integration** | Redis writes | Direct S3 writes |
| **Event Filtering** | Lambda code | S3 suffix filter |
| **Streaming** | Full book load | Streaming Parquet reader |
| **Tail Latency** | High (wait for slowest book) | Low (incremental completion) |
| **Observability** | Limited | ETA, progress, metrics |

## 14.2 Architecture Pattern Comparison

### MVP1 (Anti-Patterns)

```
❌ Redis as Data Bus
❌ BLPOP Polling
❌ Book-Level Processing
❌ In-Memory Aggregation
❌ Lambda Filters in Code
❌ Stateful Reconciliation
```

### MVP2 (Best Practices)

```
✅ S3 Shared Storage
✅ Event-Driven Push
✅ Deal-Level Parallelism
✅ Distributed S3 Aggregation
✅ S3 Suffix Filtering
✅ Stateless Reconciliation
```

## 14.3 Performance Impact

**Throughput Improvement:**
```
MVP1: 100 books/min
MVP2: 833 books/min
Improvement: 8.3x
```

**Cluster Utilization:**
```
MVP1: 60% (40% idle waiting)
MVP2: 95% (5% overhead)
Improvement: 58% more work per core
```

**Latency Reduction:**
```
MVP1: High tail latency (wait for entire book)
MVP2: Low tail latency (streaming deals)
Improvement: ~50% P95 latency reduction
```

## 14.4 Operational Improvements

**MVP1 Issues:**
- Frequent Redis memory alerts
- Pod OOMKilled errors
- BLPOP timeout issues
- Manual book retry processes
- Limited visibility into GRID progress

**MVP2 Solutions:**
- 90% Redis memory reduction
- No pod memory pressure
- Zero polling overhead
- Automatic reconciliation
- Real-time progress tracking via _progress.json

---

# 15. Deployment Architecture

## 15.1 XDE Runtime Location

**Critical Clarification:** XDE runs inside **GRID containers**, NOT inside XENA EKS.

### Component Locations

| Component | Runtime Environment | AWS Account |
|-----------|-------------------|-------------|
| FMTMService | EKS (XENA) | XENA |
| CalibrationService | EKS (XENA) | XENA |
| GRIDConnector | EKS (XENA) | XENA |
| XENALoader Lambda | Lambda (XENA) | XENA |
| Result Processor Lambda | Lambda (XENA) | XENA |
| **XDE C++ Engine** | **GRID Containers** | **GRID (External)** |

### Architecture Diagram

```
┌─────────────────────────────────────┐
│      AWS Account: XENA              │
│                                     │
│  ┌─────────────────────────────┐   │
│  │  EKS Cluster                │   │
│  │  ├─ FMTMService             │   │
│  │  ├─ CalibrationService      │   │
│  │  └─ GRIDConnector           │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │  Lambda Functions           │   │
│  │  ├─ XENALoader              │   │
│  │  └─ Result Processor        │   │
│  └─────────────────────────────┘   │
│                                     │
│  ┌─────────────────────────────┐   │
│  │  S3 Buckets                 │   │
│  │  ├─ Input                   │   │
│  │  ├─ Output                  │   │
│  │  └─ Error                   │   │
│  └─────────────────────────────┘   │
│                                     │
└─────────────────────────────────────┘
                 ↕
          (Cross-Account IAM)
                 ↕
┌─────────────────────────────────────┐
│      AWS Account: GRID              │
│                                     │
│  ┌─────────────────────────────┐   │
│  │  GRID Infrastructure        │   │
│  │                              │   │
│  │  ┌──────────────────────┐   │   │
│  │  │ XDE C++ Container    │   │   │
│  │  │ (Deal Processing)    │   │   │
│  │  └──────────────────────┘   │   │
│  │                              │   │
│  └─────────────────────────────┘   │
│                                     │
└─────────────────────────────────────┘
```

## 15.2 Deployment Sequence

### 1. XENA EKS Deployment

```bash
# Deploy XENA services
kubectl apply -f fmtm-service.yaml
kubectl apply -f calibration-service.yaml
kubectl apply -f grid-connector.yaml

# Verify deployments
kubectl get pods -n xena
```

### 2. Lambda Deployment

```bash
# Package and deploy XENALoader
aws lambda update-function-code \
  --function-name XENALoader \
  --zip-file fileb://xena-loader.zip

# Package and deploy Result Processor
aws lambda update-function-code \
  --function-name ResultProcessor \
  --zip-file fileb://result-processor.zip
```

### 3. S3 Event Configuration

```bash
# Configure S3 event notification
aws s3api put-bucket-notification-configuration \
  --bucket xena-output-prod \
  --notification-configuration file://s3-events.json
```

**s3-events.json:**
```json
{
  "QueueConfigurations": [
    {
      "Id": "ManifestCompletionEvent",
      "QueueArn": "arn:aws:sqs:REGION:ACCOUNT:xena-output-queue",
      "Events": ["s3:ObjectCreated:*"],
      "Filter": {
        "Key": {
          "FilterRules": [
            {
              "Name": "suffix",
              "Value": "manifest.json"
            }
          ]
        }
      }
    }
  ]
}
```

### 4. GRID XDE Deployment

**Note:** XDE deployment is managed by GRID team in their AWS account.

**XENA Responsibilities:**
- Provide IAM role ARN for cross-account access
- Share S3 bucket names
- Document S3 storage contract

**GRID Responsibilities:**
- Build and deploy XDE container
- Configure IAM role assumption
- Test S3 read/write access

## 15.3 Environment Configuration

**Production:**
- XENA Account: `123456789012`
- GRID Account: `987654321098`
- Region: `us-east-1`
- S3 Buckets: `xena-{input|output|error}-prod`
- SQS Queues: `xena-{input|output}-queue-prod`

**Staging:**
- XENA Account: `123456789012`
- GRID Account: `987654321098`
- Region: `us-east-1`
- S3 Buckets: `xena-{input|output|error}-staging`
- SQS Queues: `xena-{input|output}-queue-staging`

**Development:**
- XENA Account: `123456789012`
- GRID Account: `987654321098`
- Region: `us-east-1`
- S3 Buckets: `xena-{input|output|error}-dev`
- SQS Queues: `xena-{input|output}-queue-dev`

---

# 16. Monitoring and Observability

## 16.1 Key Metrics

### XENA Metrics

| Metric | Type | Source | Threshold |
|--------|------|--------|-----------|
| `books_submitted_total` | Counter | FMTMService | - |
| `books_completed_total` | Counter | Result Processor | - |
| `books_failed_total` | Counter | Reconciliation Lambda | Alert if >1% |
| `book_processing_duration_seconds` | Histogram | Result Processor | P95 <600s |
| `grid_submission_duration_seconds` | Histogram | GRIDConnector | P95 <5s |
| `manifest_processing_duration_seconds` | Histogram | XENALoader Lambda | P95 <2s |

### GRID Metrics (from _progress.json)

| Metric | Type | Source | Threshold |
|--------|------|--------|-----------|
| `dealExecutionTime` | Histogram | _progress.json | P95 <500ms |
| `queueDepth` | Gauge | XDE internal | Alert if >10K |
| `aggregationLatency` | Histogram | XDE aggregator | P95 <100ms |
| `progressWrites` | Counter | XDE | Every 60s or 500 deals |
| `reconciliationSkipCount` | Counter | XDE reconciliation | - |
| `processingRateDealsPerSec` | Gauge | _progress.json | >100 |

### Infrastructure Metrics

| Metric | Type | Source | Threshold |
|--------|------|--------|-----------|
| `sqs_queue_depth` | Gauge | CloudWatch | Alert if >1000 |
| `lambda_invocations` | Counter | CloudWatch | - |
| `lambda_errors` | Counter | CloudWatch | Alert if >1% |
| `lambda_duration` | Histogram | CloudWatch | P95 <30s |
| `s3_request_rate` | Gauge | CloudWatch | - |
| `s3_error_rate` | Gauge | CloudWatch | Alert if >1% |
| `eks_cpu_utilization` | Gauge | CloudWatch | Alert if >80% |
| `eks_memory_utilization` | Gauge | CloudWatch | Alert if >80% |

## 16.2 Alerting Rules

| Alert | Condition | Severity | Action |
|-------|-----------|----------|--------|
| High failure rate | book_failed_total >1% | Critical | PagerDuty |
| SQS backlog | queue_depth >1000 | Warning | Email alert |
| Lambda errors | lambda_errors >1% | Warning | Email alert |
| Lambda duration | duration P95 >30s | Warning | Email alert |
| EKS CPU | cpu_utilization >80% | Warning | Email alert |
| S3 error rate | s3_errors >1% | Critical | Email alert |
| Stalled book | _progress.json not updated >10min | Critical | Auto-reconciliation |

## 16.3 Distributed Tracing

**Tool:** AWS X-Ray

**Traced Components:**
- API Gateway
- Lambda (XENALoader, XENAFilter, Result Processor)
- EKS (FMTMService, GRIDConnector)
- S3 API calls
- Redis operations
- SQS operations

**Trace IDs:**
- Propagated via HTTP headers
- Linked across service boundaries
- End-to-end visibility from ingestion to completion

**Example Trace:**
```
Book Submission (trace-id: 1-2-3-4-5)
  ├─ S3 PUT (2ms)
  ├─ SQS Enqueue (5ms)
  ├─ XENALoader Lambda (450ms)
  ├─ API Gateway (10ms)
  ├─ SEPIA (5ms)
  ├─ FMTMService (15ms)
  ├─ GRIDConnector (8ms)
  ├─ GRID Job Submission (50ms)
  └─ Total: 545ms
```

## 16.4 Logging

**Centralized Logging:** CloudWatch Logs

**Log Groups:**
- `/aws/lambda/XENALoader`
- `/aws/lambda/XENAFilter`
- `/aws/lambda/ResultProcessor`
- `/aws/lambda/ReconciliationScheduler`
- `/aws/eks/xena/fmtm-service`
- `/aws/eks/xena/calibration-service`
- `/aws/eks/xena/grid-connector`
- `/aws/apigateway/xena`

**Log Retention:** 30 days (production), 7 days (dev)

**Structured Logging:**
```json
{
  "timestamp": "2026-03-05T18:00:12Z",
  "level": "INFO",
  "component": "FMTMService",
  "bookId": "eFX",
  "gridJobId": "grid-742912",
  "reconciliation": false,
  "bookRetries": 0,
  "message": "Started GRID job",
  "traceId": "1-5f8a1b2c-3d4e5f6a7b8c9d0e1f2a3b4c"
}
```

## 16.5 Dashboards

**CloudWatch Dashboard:** XENA MVP2 Production

**Panels:**
1. **Throughput** (books/min)
2. **SQS Queue Depths** (Input, Output)
3. **Lambda Invocations & Errors** (XENALoader, Result Processor)
4. **EKS Pod Health & Scaling** (FMTMService, GRIDConnector)
5. **S3 Request Rates** (GET, PUT)
6. **Redis Metrics** (Memory, Connections)
7. **GRID Processing Time** (from manifest durationSeconds)
8. **End-to-End Latency** (Submission to Completion)
9. **Progress Tracking** (from _progress.json metrics)
10. **Reconciliation Stats** (Skip counts, retry counts)

---

# 17. Reliability Guarantees

## 17.1 Failure Scenarios and Outcomes

| Failure | Impact | Outcome | Mitigation |
|---------|--------|---------|------------|
| **Single deal failure** | Deal error record | Continue processing | Write to Error bucket, count in manifest |
| **Lambda failure** | Single book delay | Automatic retry | SQS retry (3x), then DLQ |
| **FMTMService pod crash** | Request failure | Auto-healing | Kubernetes restarts pod, API Gateway retries |
| **GRID worker crash** | Partial book delay | Queue redistribution | Remaining workers continue |
| **GRID job crash** | Book failure | Reconciliation detects | Mark STALLED, auto-reconciliation |
| **S3 outage** | System halt | Wait for AWS recovery | No mitigation (AWS dependency) |
| **Redis failure** | State loss | Rebuild from S3 | Replay S3 events, rebuild state |
| **API Gateway failure** | Ingestion halt | Automatic failover | AWS managed HA |
| **Stalled book** | No progress >10min | Auto-detected | EventBridge triggers reconciliation |

## 17.2 Architectural Guarantees

### 1. High Cluster Utilization

**Guarantee:** ~95% GRID cluster utilization

**Achieved by:** Deal-level parallelism, global queue, dynamic batching

### 2. Streaming Processing

**Guarantee:** No full memory load of books

**Achieved by:** Parquet streaming reader, bounded memory, chunk-based processing

### 3. Manifest File Creation Guarantees Job Completion Event

**Guarantee:** `manifest.json` is authoritative completion signal

**Achieved by:**
- Aggregator writes manifest only when `completedDeals + failedDeals == totalDeals`
- S3 event filtered by suffix (`manifest.json`)
- Atomic write operation

### 4. External Observability

**Guarantee:** XENA can monitor GRID progress

**Achieved by:** `_progress.json` updated every 60s with ETA and metrics

### 5. Resilient Failure Handling

**Guarantee:** Deal failures don't fail books

**Achieved by:** Deal-level isolation, error records in Error bucket, partial success accepted

### 6. Scalable S3-Based Result Aggregation

**Guarantee:** No memory bottleneck for results

**Achieved by:** Direct S3 writes, no intermediate aggregation, distributed storage

### 7. Idempotent Lambda Processing

**Guarantee:** Duplicate events don't cause duplicate processing

**Achieved by:**
- SQS deduplication (content-based)
- Redis idempotency keys
- S3 conditional writes (if-not-exists)

**Example:**
```python
def lambda_handler(event, context):
    for record in event['Records']:
        message_id = record['messageId']
        
        # Check idempotency key
        if redis.exists(f"processed:{message_id}"):
            print(f"Duplicate message {message_id}, skipping")
            continue
        
        # Process message
        process_manifest(record)
        
        # Mark as processed
        redis.setex(f"processed:{message_id}", 3600, "1")
```

## 17.3 SLA Targets

**Availability:** 99.9% (three nines)

**Throughput:** 833 books/min sustained

**Latency:**
- XENA overhead: <10s (P95)
- Total end-to-end: <5 min for small books, <60 min for large books

**Data Durability:** 99.999999999% (S3 guarantee)

**Error Rate:** <0.1% (book-level failures)

**Recovery Time:**
- Reconciliation: <5 min to detect stalled book
- Automatic retry: Triggered within 10 min of stall detection

---

# 18. Service Responsibilities Matrix

## 18.1 Responsibility Assignment

| Component | Storage | Decoupling | Event Handling | Orchestration | Compute Bridge | Calculation |
|-----------|---------|------------|----------------|---------------|----------------|-------------|
| **S3** | ✅ | | | | | |
| **SQS** | | ✅ | | | | |
| **Lambda** | | | ✅ | | | |
| **FMTMService** | | | | ✅ | | |
| **GRIDConnector** | | | | | ✅ | |
| **MTO GRID (XDE)** | | | | | | ✅ |
| **Redis** | Metadata only | | | | | |

## 18.2 Clear Separation of Concerns

**CalibrationService (upstream of GRID execution):**
- External API
- Book submission
- GRID job invocation via GRIDConnector

**FMTMService (manifest processing):**
- Manifest processing
- Redis state updates
- Completion orchestration

**GRIDConnector (control plane bridge):**
- Technical integration with GRID
- Pass S3 paths (no payload)
- Return acknowledgment

**MTO GRID / XDE (compute plane):**
- Read from S3 directly
- Process deals in parallel
- Write to S3 directly
- Reconciliation logic

**Important:** CalibrationService must NOT contain GRID execution logic. Separation is clean.

## 18.3 Architectural Layers

```
┌─────────────────────────────────────────┐
│         Presentation Layer              │  ← API Gateway, SEPIA
├─────────────────────────────────────────┤
│      Business Orchestration Layer       │  ← FMTMService, CalibrationService
├─────────────────────────────────────────┤
│      External Integration Layer         │  ← GRIDConnector
├─────────────────────────────────────────┤
│         Compute Layer                   │  ← MTO GRID (XDE C++)
├─────────────────────────────────────────┤
│         Event Layer                     │  ← SQS, Lambda
├─────────────────────────────────────────┤
│         Data Layer                      │  ← S3 (payloads), Redis (metadata)
└─────────────────────────────────────────┘
```

---

# Appendices

## Appendix A: Glossary

**Book:** A collection of deals representing a full Murex book export (Parquet format)

**Deal:** Individual financial instrument record within a book

**FMTM:** Full Market-To-Market valuation process

**XDE:** eXtended Data Engine - MTO's optimized C++ compute library running in GRID containers

**Manifest:** Authoritative completion signal file (`manifest.json`)

**Progress Marker:** Periodic status file (`_progress.json`) updated every 60s or 500 deals

**Aggregator:** Component that tracks deal completion and writes manifest

**Idempotency:** Property ensuring duplicate requests produce same result

**BLPOP:** Redis blocking list pop command (anti-pattern for this use case)

**Cross-Account IAM:** AWS pattern where one account assumes a role in another

**Streaming:** Processing data incrementally without loading entirely into memory

**Reconciliation:** Processing mode that reuses existing S3 results to minimize recomputation

**Suffix Filtering:** S3 event configuration that triggers events only for files matching a specific suffix (e.g., `manifest.json`)

## Appendix B: Acronyms

| Acronym | Full Term |
|---------|-----------|
| FMTM | Full Market-To-Market |
| XDE | eXtended Data Engine |
| MVP | Minimum Viable Product |
| IAM | Identity and Access Management |
| SQS | Simple Queue Service |
| DLQ | Dead Letter Queue |
| HPA | Horizontal Pod Autoscaler |
| NLB | Network Load Balancer |
| EKS | Elastic Kubernetes Service |
| RTO | Recovery Time Objective |
| RPO | Recovery Point Objective |
| SLA | Service Level Agreement |
| TTL | Time To Live |
| SRP | Single Responsibility Principle |
| ETA | Estimated Time to Arrival/Completion |

## Appendix C: References

**Internal Documents:**
- XENA MVP1 Architecture Documentation
- GRID Integration Proposal (March 2026)
- Cross-Account Security Standards
- XENA MVP2 FMTM Technical Specification
- XDE C++ Processing Engine Documentation

**External References:**
- AWS S3 Best Practices: https://docs.aws.amazon.com/s3/
- AWS S3 Event Notifications: https://docs.aws.amazon.com/AmazonS3/latest/userguide/NotificationHowTo.html
- AWS SQS Documentation: https://docs.aws.amazon.com/sqs/
- AWS Lambda Best Practices: https://docs.aws.amazon.com/lambda/
- Apache Parquet Documentation: https://parquet.apache.org/
- Apache Arrow C++: https://arrow.apache.org/docs/cpp/

---

# Document Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-03-05 | Architecture Team | Initial release |
| 2.0 | 2026-03-08 | Architecture Team | Added dual execution modes, reconciliation details, progress tracking, S3 suffix filtering clarification, XDE processing model expansion, component separation refinements |

---

**Document Classification:** Confidential  
**Document Owner:** XENA Architecture Team  
**Next Review Date:** 2026-06-05

---

*End of Document*
