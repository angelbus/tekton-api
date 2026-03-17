# XENA MVP2
## Full Market-To-Market (FMTM)
### Event-Driven Serverless Architecture

**Shared Storage, High-Throughput, Deal-Level Execution**

---

**Architecture Documentation**  
Version 3.0 | March 16, 2026  
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
20. [Architecture Principles](#architecture-principles)

---

# Executive Summary

XENA MVP2 represents a **fundamental architectural transformation** from MVP1, introducing a **fully event-driven serverless architecture** built on AWS managed services. The control plane runs entirely on **Lambda, S3 events, SQS, and EventBridge**, while heavy computation remains delegated to the **GRID HPC infrastructure** via the **XDE (XENA Deal Engine)**.

## Key Architecture Model

**Serverless Control Plane** + **Shared S3 Storage Contract** + **GRID HPC Data Plane**

Where:

| Layer | Technology |
|-------|------------|
| **Control plane** | Lambda + EventBridge + SQS |
| **Data exchange** | S3 shared storage |
| **Compute plane** | GRID HTC cluster |
| **Execution engine** | XDE C++ |

This architecture eliminates **microservices/containers overhead**, removes the **BLPOP polling anti-pattern**, and achieves **833+ books per minute sustained throughput** with **drastically reduced operational costs**.

## Dual Execution Modes

XENA supports two execution modes: **standard processing** and **reconciliation processing**. Reconciliation jobs reuse existing deal results stored in S3 when available, minimizing recomputation and enabling fast book recovery. This stateless idempotency check allows XENA to efficiently recover from failures without requiring external state management.

## Critical Paradigm Shift

**From:** Book-level execution with microservices, containers, and Redis polling (BLPOP anti-pattern)

**To:** Deal-level parallel execution with serverless orchestration, S3 shared storage, and S3 event-driven completion triggered only by manifest.json creation (suffix filtering)

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
- ✅ **Serverless-first** - No microservices/containers required, pure Lambda orchestration
- ✅ **Massive cost reduction** - No 24/7 cluster charges, pay-per-use pricing

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

This document provides the **authoritative technical specification** for XENA MVP2's Full Market-To-Market (FMTM) **serverless architecture**, including:

- Complete serverless system architecture and component specifications
- Deal-level distributed processing model
- Shared S3 storage contract and event-driven patterns
- Cross-account security model
- XDE C++ processing engine integration
- Completion manifest contract and reconciliation mechanisms
- Performance characteristics and reliability guarantees
- Progress tracking and observability patterns

This document describes the interaction between **XENA serverless components** (Lambda functions), **GRID infrastructure**, and the **XDE compute engine** responsible for deal-level distributed execution.

## Target Audience

- XENA Development Team
- GRID Engineering Team
- Platform Architecture Review Board
- Operations and SRE Teams
- Business Stakeholders

## Component Definitions

- **XENALoader**: Lambda function handling S3 input events and GRID job submission
- **GRIDConnector**: Lambda function calling GRID REST API
- **OutputHandler**: Lambda function processing manifest.json and _progress.json
- **ErrorHandler**: Lambda function processing XDE errors
- **ReconciliationJob**: Lambda function detecting stalled jobs
- **XDE (eXtended Data Engine)**: C++ execution engine running in GRID for parallel deal processing
- **EventBridge**: AWS orchestration service for periodic reconciliation checks

---

# 1. Architecture Overview

## 1.1 Serverless Component Architecture

XENA MVP2 consists of the following **serverless** architectural components:

### XENA Serverless Components

| Component | Type | Responsibility |
|-----------|------|----------------|
| **XENALoader** | Lambda | Handles S3 ingestion events and submits jobs to GRID |
| **GRIDConnector** | Lambda | Calls GRID REST API to start processing |
| **OutputHandler** | Lambda | Processes _progress.json and manifest.json events |
| **ErrorHandler** | Lambda | Processes error outputs written by XDE |
| **ReconciliationJob** | Lambda | Detects stalled processing |
| **EventBridge Scheduler** | EventBridge | Triggers reconciliation every 10 minutes |

### GRID Components (External AWS Account)

| Component | Type | Responsibility |
|-----------|------|----------------|
| **XDE Execution Engine** | C++ Application | Deal-level parallel processing |
| **MTO GRID Cluster** | HPC Infrastructure | Compute resources |

### AWS Infrastructure

| Component | Type | Responsibility |
|-----------|------|----------------|
| **S3 Buckets** | Object Storage | Input, Output, Error storage |
| **SQS Queues** | Message Queue | Event decoupling (Input, Output, DLQ) |
| **EventBridge** | Event Scheduler | Reconciliation triggers |
| **Redis** | Cache | Metadata storage only |

## 1.2 Separation of Responsibilities

| Component | Responsibility |
|-----------|----------------|
| **XENALoader** | S3 event handling, job type determination, GRID submission |
| **GRIDConnector** | GRID REST API calls, job payload construction |
| **OutputHandler** | Progress/manifest processing, Redis updates |
| **ErrorHandler** | Error logging, metrics emission |
| **ReconciliationJob** | Stalled job detection, retry triggering |
| **XDE** | Deal execution, parallel processing, S3 writes |

**Critical Principle:** Each Lambda function has a **single, focused responsibility**. This serverless architecture eliminates orchestration complexity while maintaining clear separation of concerns.

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

### 2. Control Plane - Lambda + SQS + EventBridge

**Serverless control plane** orchestrates all processing:
- Lambda functions handle events
- SQS provides message queuing
- EventBridge schedules reconciliation
- No containers or microservices required
- Pay-per-use pricing model

### 3. Compute Plane - GRID Parallel Processing

MTO GRID XDE C++ engine:
- Reads books **directly from S3** (streaming)
- Processes deals in parallel (multi-threaded)
- Writes results **directly to S3** (no intermediate hops)
- Cross-account IAM role access
- No payload relay through services

### 4. Event Plane - S3 Events + SQS Decoupling

S3 ObjectCreated events trigger SQS queues:
- Decouples ingestion from processing
- Backpressure control
- At-least-once delivery semantics
- DLQ support for failures
- Massive parallelism via Lambda

### 5. Metadata Plane - Redis

Redis stores **only control metadata**:
- Book registration
- Idempotency keys
- Processing state
- Typical payload: ~2 KB per book

**NO payload storage. NO BLPOP. NO large objects.**

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
    ↓ (Call)
GRIDConnector (Lambda)
    ↓ (REST API)
GRID Cluster
    ↓ (Launch)
XDE Execution Engine
```

### Processing Path (GRID Direct S3 Access)

```
XDE Engine
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
OutputHandler (Lambda)
    ↓ (Update Redis, notify completion)
Downstream Systems
```

## 1.6 System Context Diagram

```
┌─────────────────┐         ┌──────────────────────────────┐         ┌─────────────────┐
│  Data Lake /    │         │     AWS Account - XENA       │         │ AWS Account -   │
│  Murex          │────────▶│  Serverless Orchestration    │◀───────▶│ GRID Compute    │
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
- Memory pressure in services
- Payload duplication over network
- Polling inefficiency (constant BLPOP calls)
- Redis misused as data bus
- Poor throughput scaling
- Requires containers/microservices

### New Pattern (MVP2 - Correct Architecture)

```
GRID → S3 (direct write) → S3 Event (suffix: manifest.json) → SQS → Lambda → XENA reacts
```

**Benefits:**
- ✅ **Zero polling** - Event-driven push model
- ✅ **Zero payload duplication** - Single write to S3
- ✅ **Suffix filtering** - Events only for manifest.json, not intermediate deal files
- ✅ **Atomic completion signal** - manifest.json written last
- ✅ **Serverless** - No containers required
- ✅ **Scalable** - S3 handles any write volume
- ✅ **Durable** - 99.999999999% S3 durability

**Critical:** Filtering at the **S3 event configuration** avoids unnecessary Lambda invocations when intermediate deal files are written.

## 2.2 Three Core Files in XDE Processing

XDE produces three key files during job execution:

### 1. `_progress.json`

**Purpose:** Periodic checkpoint for observability and reconciliation recovery

**Update Frequency:** Every 10 minutes (configurable)

**Contents:**
```json
{
  "bookId": "eFX",
  "processedDeals": 52300,
  "totalDeals": 80000,
  "lastProcessingDeal": "deal-918273",
  "timestamp": "2026-03-05T12:30:00Z"
}
```

**Use Cases:**
- Monitoring GRID progress from XENA
- Detecting stalled jobs
- Reconciliation recovery
- ETA calculation

### 2. Deal Results (`deal_results.parquet`)

**Purpose:** Individual deal processing results

**Format:** Parquet (columnar storage)

**Written:** Incrementally as deals complete

### 3. `manifest.json`

**Purpose:** **Atomic completion signal** for the entire job

**Written:** LAST, only when all deals complete

**Contents:**
```json
{
  "bookId": "eFX",
  "inputFile": "s3://xena-input/fmtm/book.parquet",
  "totalDeals": 48178,
  "completedDeals": 47000,
  "failedDeals": 1178,
  "status": "COMPLETED",
  "startTime": "2026-03-05T18:00:12Z",
  "endTime": "2026-03-05T18:07:42Z",
  "durationSeconds": 450,
  "resultsPath": "s3://xena-output/eFX/",
  "errorPath": "s3://xena-error/eFX/",
  "gridJobId": "grid-742912",
  "version": "1.0"
}
```

**Completion Contract:** `manifest.json` is written **last** and represents the **atomic completion signal** for the job. Its presence triggers the S3 event that notifies XENA of job completion.

## 2.3 GRID Direct S3 Access Pattern

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

## 3.1 XENALoader Lambda

### Responsibility

XENALoader receives S3 input events and orchestrates job submission to GRID.

### Trigger

```yaml
Event Source: S3 Input Bucket
Event Type: ObjectCreated
Prefix Filters:
  - calibration/
  - fmtm/
Target: XENALoader Lambda (direct invocation)
```

### Processing Logic

```python
def lambda_handler(event, context):
    for record in event['Records']:
        # Extract S3 key
        s3_key = record['s3']['object']['key']
        bucket = record['s3']['bucket']['name']
        
        # Determine job type from prefix
        if s3_key.startswith('calibration/'):
            job_type = 'CALIBRATION'
        elif s3_key.startswith('fmtm/'):
            job_type = 'FMTM'
        else:
            raise ValueError(f"Unknown job type for key: {s3_key}")
        
        # Read Parquet metadata
        metadata = read_parquet_metadata(bucket, s3_key)
        total_deals = metadata['num_rows']
        book_size = metadata['file_size']
        
        # Extract bookId from key
        book_id = extract_book_id(s3_key)
        
        # Build GRID job payload
        payload = {
            'bookId': book_id,
            'jobType': job_type,
            'inputFile': f's3://{bucket}/{s3_key}',
            'totalDeals': total_deals,
            'bookSize': book_size,
            'reconciliation': False,
            'bookRetries': 0
        }
        
        # Register in Redis
        register_book(book_id, payload)
        
        # Invoke GRIDConnector Lambda
        invoke_lambda('GRIDConnector', payload)
```

### Scaling Configuration

- **Memory:** 512 MB
- **Timeout:** 30 seconds
- **Concurrency:** 100 (reserved)
- **Retry:** 3 attempts
- **DLQ:** Enabled

## 3.2 GRIDConnector Lambda

### Responsibility

GRIDConnector calls the GRID REST API to start processing.

### Processing Logic

```python
def lambda_handler(event, context):
    # Extract job payload
    book_id = event['bookId']
    job_type = event['jobType']
    
    # Build GRID API payload
    grid_payload = {
        'bookId': book_id,
        'jobType': job_type,
        'inputFile': event['inputFile'],
        'totalDeals': event['totalDeals'],
        'bookSize': event['bookSize'],
        'reconciliation': event.get('reconciliation', False),
        'bookRetries': event.get('bookRetries', 0)
    }
    
    # Call GRID REST API
    response = requests.post(
        f'{GRID_API_URL}/jobs',
        json=grid_payload,
        headers={'Authorization': f'Bearer {get_grid_token()}'}
    )
    
    # Extract GRID job ID
    grid_job_id = response.json()['gridJobId']
    
    # Update Redis with gridJobId
    update_book_metadata(book_id, {'gridJobId': grid_job_id})
    
    return {'gridJobId': grid_job_id}
```

### Scaling Configuration

- **Memory:** 256 MB
- **Timeout:** 15 seconds
- **Concurrency:** 50 (reserved)
- **Retry:** 3 attempts
- **DLQ:** Enabled

## 3.3 OutputHandler Lambda

### Responsibility

OutputHandler processes `_progress.json` and `manifest.json` events from S3 Output Bucket.

### Trigger

```yaml
Event Source: S3 Output Bucket
Event Type: ObjectCreated
Suffix Filters:
  - _progress.json
  - manifest.json
Target: SQS Output Queue → OutputHandler Lambda
```

### Processing Logic

```python
def lambda_handler(event, context):
    for record in event['Records']:
        # Parse SQS message (contains S3 event)
        s3_event = json.loads(record['body'])
        s3_key = s3_event['Records'][0]['s3']['object']['key']
        bucket = s3_event['Records'][0]['s3']['bucket']['name']
        
        # Download file
        obj = s3.get_object(Bucket=bucket, Key=s3_key)
        data = json.loads(obj['Body'].read())
        
        # Route based on file type
        if s3_key.endswith('_progress.json'):
            handle_progress(data)
        elif s3_key.endswith('manifest.json'):
            handle_manifest(data)

def handle_progress(data):
    """Update Redis with progress"""
    book_id = data['bookId']
    redis.hset(f'xena:book:{book_id}', mapping={
        'processedDeals': data['processedDeals'],
        'lastProgressTimestamp': data['timestamp']
    })

def handle_manifest(data):
    """Mark book completed and remove from pending set"""
    book_id = data['bookId']
    
    # Update status
    redis.hset(f'xena:book:{book_id}', mapping={
        'status': 'COMPLETED',
        'completedDeals': data['completedDeals'],
        'failedDeals': data['failedDeals'],
        'completionTimestamp': data['endTime']
    })
    
    # Remove from pending books
    redis.srem('xena:pending_books', book_id)
    
    # Emit completion notification
    emit_completion_event(data)
```

**Important Rule:** `manifest.json` is always written **last** by XDE. This acts as the completion signal.

### Scaling Configuration

- **Memory:** 512 MB
- **Timeout:** 30 seconds
- **Batch Size:** 10 messages
- **Concurrency:** 100 (reserved)
- **Retry:** 3 attempts
- **DLQ:** Enabled

## 3.4 ErrorHandler Lambda

### Responsibility

ErrorHandler processes failures written by XDE to the Error bucket.

### Trigger

```yaml
Event Source: S3 Error Bucket
Event Type: ObjectCreated
Target: SQS Error Queue → ErrorHandler Lambda
```

### Processing Logic

```python
def lambda_handler(event, context):
    for record in event['Records']:
        # Parse SQS message
        s3_event = json.loads(record['body'])
        s3_key = s3_event['Records'][0]['s3']['object']['key']
        bucket = s3_event['Records'][0]['s3']['bucket']['name']
        
        # Download error file
        obj = s3.get_object(Bucket=bucket, Key=s3_key)
        error_data = json.loads(obj['Body'].read())
        
        # Log failed deals
        logger.error(f"Deal failed: {error_data}")
        
        # Update Redis metadata
        book_id = error_data['bookId']
        redis.hincrby(f'xena:book:{book_id}', 'failedDeals', 1)
        
        # Emit operational metrics
        cloudwatch.put_metric_data(
            Namespace='XENA',
            MetricData=[{
                'MetricName': 'DealFailures',
                'Value': 1,
                'Unit': 'Count'
            }]
        )
```

### Scaling Configuration

- **Memory:** 256 MB
- **Timeout:** 15 seconds
- **Batch Size:** 10 messages
- **Concurrency:** 50 (reserved)
- **Retry:** 3 attempts
- **DLQ:** Enabled

## 3.5 ReconciliationJob Lambda

### Responsibility

ReconciliationJob detects stalled processing and triggers retry with reconciliation mode.

### Trigger

```yaml
Event Source: EventBridge Scheduler
Schedule: rate(10 minutes)
Target: ReconciliationJob Lambda
```

### Processing Logic

```python
def lambda_handler(event, context):
    # Retrieve pending books from Redis
    pending_books = redis.smembers('xena:pending_books')
    
    now = datetime.utcnow()
    
    for book_id in pending_books:
        # Get book metadata
        book = redis.hgetall(f'xena:book:{book_id}')
        
        # Read _progress.json from S3
        try:
            progress = download_progress_file(book_id)
            last_update = parse_timestamp(progress['timestamp'])
        except FileNotFoundError:
            # No progress file yet, check start time
            start_time = parse_timestamp(book['startTime'])
            elapsed = (now - start_time).seconds
            if elapsed > MAX_INITIAL_WAIT:
                mark_stalled(book_id)
            continue
        
        # Compare timestamp
        elapsed = (now - last_update).seconds
        
        if elapsed > MAX_PROCESSING_TIME:
            # Mark as stalled
            redis.hset(f'xena:book:{book_id}', 'status', 'STALLED')
            
            # Trigger reconciliation
            reconcile_payload = {
                'bookId': book_id,
                'jobType': book['jobType'],
                'inputFile': book['inputFile'],
                'totalDeals': book['totalDeals'],
                'bookSize': book['bookSize'],
                'reconciliation': True,
                'bookRetries': int(book.get('bookRetries', 0)) + 1
            }
            
            # Invoke GRIDConnector with reconciliation=true
            invoke_lambda('GRIDConnector', reconcile_payload)
            
            # Emit alert
            emit_alert(f"Book {book_id} stalled, triggered reconciliation")
```

### Constants

```python
MAX_INITIAL_WAIT = 300  # 5 minutes
MAX_PROCESSING_TIME = 600  # 10 minutes
```

### Scaling Configuration

- **Memory:** 512 MB
- **Timeout:** 60 seconds
- **Concurrency:** 1 (single execution)
- **Retry:** Not needed (scheduled)

## 3.6 XDE Execution Engine

### Overview

XDE (eXtended Data Engine) is the C++ execution engine running in **GRID** (not XENA). It implements the deal-level parallel processing model with streaming, dynamic batching, and reconciliation support.

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
  - If success → upload to Output bucket (`deal_results.parquet`)
  - If failed → upload to Error bucket (`failed_deals.json`)

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
    for (const auto& deal : book.deals) {
        // Check if result already exists in S3
        if (resultExistsInS3(deal.dealId)) {
            // Skip processing, count as completed
            processedDeals++;
            completedDeals++;
            continue;
        }
        
        // Process only missing/failed deals
        enqueueDeal(deal);
    }
}
```

**Method:** `resultExistsInS3(dealId)`
- Checks S3 Output bucket for `deal_results.parquet` containing dealId
- HEAD request (lightweight, no download)
- Returns true if file exists, false otherwise

**Stateless:** No Redis or external state required for idempotency check.

### Processing Pipeline

```
1. Submit job (receive payload from GRIDConnector)
2. Load payload from GRID scheduler
3. Decompose book into deals (streaming Parquet reader)
4. Enqueue deals to worker queue (dynamic batching)
5. Process deals in worker threads (parallel execution)
6. Aggregate results (single aggregator thread)
7. Periodically write _progress.json (every 10 min or 500 deals)
8. Write manifest.json on completion (atomic signal)
```

### Dynamic Batch Sizing

To achieve **99% cluster utilization**, XDE uses adaptive batch sizing:

```cpp
size_t computeBatchSize() const {
    const uint64_t remaining = totalDeals - processedDeals;
    
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
retryDelay = baseDelayMs * std::pow(2, attempt)
```

**Example Delays:**
```
retry 1 → 100ms
retry 2 → 200ms
retry 3 → 400ms
```

**Implementation:**
```cpp
Result processDealWithRetry(const Deal& deal) {
    int retries = 0;
    
    while (true) {
        try {
            return computeFMTM(deal);  // Attempt processing
        } catch (const std::exception& e) {
            if (retries >= maxRetries) {
                // Mark as failed, write to error bucket
                return Result{deal.dealId, false, e.what()};
            }
            
            const int delay = baseDelayMs * std::pow(2, retries);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            retries++;
        }
    }
}
```

### Progress Writing Logic

```cpp
bool shouldWriteProgress() const {
    const auto now = std::chrono::system_clock::now();
    const auto elapsed = now - lastProgressWrite;
    
    // Max interval: 10 minutes (configurable)
    if (elapsed > std::chrono::minutes(progressWriteIntervalMinutes)) {
        return true;
    }
    
    // Every 500 deals (configurable)
    if (processedDeals % progressDealThreshold == 0) {
        return true;
    }
    
    return false;
}
```

**Configurable Parameters:**
- `progressWriteIntervalMinutes = 10` (default)
- `progressDealThreshold = 500` (default)

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
        "arn:aws:s3:::xena-input/*"
      ]
    },
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject"
      ],
      "Resource": [
        "arn:aws:s3:::xena-output/*",
        "arn:aws:s3:::xena-error/*"
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
      "Resource": "arn:aws:s3:::xena-output/*",
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

## 5.1 S3 Bucket Layout

### Input Buckets

```
s3://xena-input/
├── calibration/
│   └── book.parquet
└── fmtm/
    └── book.parquet
```

**Example objects:**
```
s3://xena-input/calibration/book.parquet
s3://xena-input/fmtm/book.parquet
```

**Prefix Filters:**
- `calibration/` → Triggers XENALoader for calibration jobs
- `fmtm/` → Triggers XENALoader for FMTM jobs

### Output Buckets

```
s3://xena-output/
└── {bookId}/
    ├── deal_results.parquet
    ├── _progress.json
    └── manifest.json
```

**Example structure:**
```
s3://xena-output/eFX/
├── deal_results.parquet
├── _progress.json
└── manifest.json
```

**Suffix Filters:**
- `_progress.json` → Routed to SQS Output Queue
- `manifest.json` → Routed to SQS Output Queue

### Error Bucket

```
s3://xena-error/
└── {bookId}/
    └── failed_deals.json
```

**Example:**
```
s3://xena-error/eFX/failed_deals.json
```

## 5.2 S3 Event Configuration

### Input Bucket Event

**Configuration:**
```yaml
Bucket: xena-input
Event: ObjectCreated:*
Prefix Filters:
  - calibration/
  - fmtm/
Target: XENALoader Lambda (direct invocation)
```

**Why Direct Invocation?**
- Input events are infrequent (book uploads)
- No need for SQS buffering
- Immediate processing desired

### Output Bucket Event

**Configuration:**
```yaml
Bucket: xena-output
Event: ObjectCreated:*
Suffix Filters:
  - _progress.json
  - manifest.json
Target: SQS Output Queue
```

**Why SQS?**
- Avoids thousands of Lambda triggers for deal result files
- Provides buffering and retry capabilities
- Decouples S3 events from Lambda processing

**Critical:** This avoids invoking Lambda for every `deal_results.parquet` write, which would be thousands of invocations per book.

### Error Bucket Event

**Configuration:**
```yaml
Bucket: xena-error
Event: ObjectCreated:*
Target: SQS Error Queue
```

## 5.3 File Purpose Summary

| File | Purpose | Update Frequency | Required for Completion |
|------|---------|------------------|------------------------|
| `book.parquet` | Input data | Once (upload) | No |
| `deal_results.parquet` | Individual results | Incremental (as deals complete) | No |
| `_progress.json` | Periodic checkpoint | Every 10 min or 500 deals | No |
| `manifest.json` | **Atomic completion signal** | **Once (LAST)** | **YES** |

## 5.4 Storage Contracts

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
- Triggers S3 event → SQS → OutputHandler → XENA notification
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

### 1. Receive Job

**Trigger:** GRID Scheduler receives job from GRIDConnector Lambda

**Payload:**
```json
{
  "bookId": "eFX",
  "jobType": "FMTM",
  "inputFile": "s3://xena-input/fmtm/book.parquet",
  "totalDeals": 48178,
  "bookSize": 850000000,
  "reconciliation": false,
  "bookRetries": 0
}
```

### 2. Initialize XDE

**Action:** XDE engine initializes

**Setup:**
```cpp
XDEEngine engine(jobPayload);
engine.initializeThreadPools();
engine.initializeS3Client();  // Cross-account IAM role
engine.initializeCounters();
```

### 3. Stream Book Decomposition

**Streaming Parquet Reader:**
```cpp
ParquetStreamReader reader(job.inputFile);

while (reader.hasNext()) {
    Deal deal = reader.nextDeal();
    
    // Reconciliation check
    if (job.reconciliation && resultExistsInS3(deal.dealId)) {
        processedDeals++;
        completedDeals++;
        continue;
    }
    
    dealQueue.push(deal);
    totalDeals++;
}
```

**Critical:** Never materialize entire book in memory.

### 4. Dynamic Batching

**Batch Optimization:**
```cpp
while (!dealQueue.empty()) {
    const size_t batchSize = computeBatchSize();
    DealBatch batch;
    
    for (size_t i = 0; i < batchSize && !dealQueue.empty(); i++) {
        batch.deals.push_back(dealQueue.pop());
    }
    
    workerQueue.push(std::move(batch));
}
```

### 5. Parallel Processing

**Worker Thread Logic:**
```cpp
void workerThread() {
    while (running) {
        DealBatch batch;
        if (!workerQueue.pop(batch, timeout)) continue;
        
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

### 6. Result Aggregation

**Aggregator Thread Logic:**
```cpp
void aggregatorThread() {
    while (processedDeals < totalDeals) {
        Result result;
        if (!resultQueue.pop(result, timeout)) continue;
        
        // Update atomic counters
        processedDeals.fetch_add(1);
        if (result.success) {
            completedDeals.fetch_add(1);
        } else {
            failedDeals.fetch_add(1);
        }
        
        lastProcessingDeal = result.dealId;
        
        // Forward to S3 writers
        writeQueue.push(std::move(result));
        
        // Periodic progress write
        if (shouldWriteProgress()) {
            writeProgress();
        }
    }
    
    // All deals processed - write manifest
    writeManifest();
}
```

### 7. Periodic Progress Writing

**Frequency:** Every 10 minutes OR every 500 deals

**Implementation:**
```cpp
void writeProgress() {
    const auto now = std::chrono::system_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime
    ).count();
    
    const double rate = static_cast<double>(processedDeals) / elapsed;
    const uint64_t remaining = totalDeals - processedDeals;
    const uint64_t eta = static_cast<uint64_t>(remaining / rate);
    
    json progress = {
        {"bookId", job.bookId},
        {"processedDeals", processedDeals.load()},
        {"totalDeals", totalDeals},
        {"lastProcessingDeal", lastProcessingDeal},
        {"timestamp", formatTimestamp(now)}
    };
    
    s3Client.putObject(
        job.outputPrefix + "/_progress.json",
        progress.dump()
    );
    
    lastProgressWrite = now;
}
```

### 8. Manifest Generation

**Condition:**
```cpp
if (completedDeals + failedDeals == totalDeals) {
    writeManifest();
}
```

**Implementation:**
```cpp
void writeManifest() {
    const auto now = std::chrono::system_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime
    ).count();
    
    json manifest = {
        {"bookId", job.bookId},
        {"inputFile", job.inputFile},
        {"totalDeals", totalDeals},
        {"completedDeals", completedDeals.load()},
        {"failedDeals", failedDeals.load()},
        {"status", "COMPLETED"},
        {"startTime", formatTimestamp(startTime)},
        {"endTime", formatTimestamp(now)},
        {"durationSeconds", duration},
        {"resultsPath", job.outputPrefix},
        {"errorPath", job.errorPrefix},
        {"gridJobId", job.gridJobId},
        {"version", "1.0"}
    };
    
    // Write manifest.json LAST
    s3Client.putObject(
        job.outputPrefix + "/manifest.json",
        manifest.dump()
    );
}
```

**Critical:** This is the **LAST** file written. Its creation triggers the S3 event.

## 6.2 Reconciliation Processing

### Behavior

```cpp
if (job.reconciliation) {
    // Increment book retry counter
    job.bookRetries++;
    
    uint64_t skippedDeals = 0;
    
    // For each deal in the book
    while (reader.hasNext()) {
        Deal deal = reader.nextDeal();
        
        // Idempotency check: Does result already exist in S3?
        if (resultExistsInS3(deal.dealId)) {
            // Skip processing, count as completed (reuse existing result)
            processedDeals.fetch_add(1);
            completedDeals.fetch_add(1);
            skippedDeals++;
            continue;
        }
        
        // Only process missing/failed deals
        dealQueue.push(deal);
    }
    
    logger.info("Reconciliation: skipped {} deals, processing {} deals",
                skippedDeals, totalDeals - skippedDeals);
}
```

### S3 Idempotency Check

**Implementation:**
```cpp
bool XDEEngine::resultExistsInS3(const std::string& dealId) const {
    const std::string key = job.outputPrefix + "/deal_results.parquet";
    
    try {
        // HEAD request (lightweight, no download)
        auto response = s3Client.headObject(bucket, key);
        
        // Additional check: verify dealId exists in parquet file metadata
        // This could be optimized with a manifest file listing completed dealIds
        return response.isSuccess();
        
    } catch (const Aws::S3::S3Exception& e) {
        if (e.GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY) {
            return false;
        }
        throw;  // Rethrow other errors
    }
}
```

**Optimization:** Consider maintaining a separate `_completed_deals.json` file listing all completed dealIds for faster lookups during reconciliation.

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
    
    // ONLY after loop completes (all deals processed)
    // Verify count before writing manifest
    const uint64_t processed = processedDeals.load();
    const uint64_t completed = completedDeals.load();
    const uint64_t failed = failedDeals.load();
    
    if (completed + failed != totalDeals) {
        throw std::runtime_error(
            "Deal count mismatch: completed(" + std::to_string(completed) +
            ") + failed(" + std::to_string(failed) +
            ") != total(" + std::to_string(totalDeals) + ")"
        );
    }
    
    // Safe to write manifest
    writeManifest();
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
S3 Event (ObjectCreated:*, suffix: manifest.json)
    ↓
SQS Output Queue
    ↓
OutputHandler Lambda
    ↓
Update Redis: status=COMPLETED, remove from pending_books
    ↓
Notify downstream systems
```

**Critical:** The S3 event is **filtered by suffix** (`manifest.json`) to avoid triggering for intermediate result files.

---

# 8. Event-Driven Architecture

## 8.1 Complete Event Flow

```
DataLake writes input parquet
        ↓
S3 Event (input bucket, prefix: fmtm/)
        ↓
XENALoader Lambda
        ↓ (Read Parquet metadata, build payload)
GRIDConnector Lambda
        ↓ (Call GRID REST API)
GRID Scheduler
        ↓ (Launch XDE)
XDE Engine
        ↓ (Process deals in parallel)
        ├── Write deal_results.parquet (incremental)
        ├── Write _progress.json (every 10 min)
        └── Write manifest.json (LAST, completion signal)
                ↓
S3 Event (output bucket, suffix: manifest.json)
        ↓
SQS Output Queue
        ↓ (Trigger)
OutputHandler Lambda
        ↓ (Parse manifest, update Redis)
Redis update + completion notification
```

### Error Path

```
XDE writes errors
      ↓
S3 error bucket
      ↓
S3 Event
      ↓
SQS Error Queue
      ↓ (Trigger)
ErrorHandler Lambda
      ↓ (Log, metrics, alerts)
```

### Reconciliation Path

```
EventBridge Scheduler (every 10 min)
      ↓ (Trigger)
ReconciliationJob Lambda
      ↓ (Check _progress.json timestamp)
      ↓ (Detect stalled books)
      ↓ (Build reconciliation payload)
GRIDConnector Lambda
      ↓ (reconciliation=true, bookRetries++)
GRID Scheduler
      ↓
XDE Engine
      ↓ (Skip existing deals, process missing)
```

## 8.2 Event Sources

### S3 Events

| Bucket | Event Type | Filter Type | Filter Value | Target |
|--------|------------|-------------|--------------|--------|
| xena-input | ObjectCreated | Prefix | `calibration/` | XENALoader Lambda |
| xena-input | ObjectCreated | Prefix | `fmtm/` | XENALoader Lambda |
| xena-output | ObjectCreated | Suffix | `_progress.json` | SQS Output Queue |
| xena-output | ObjectCreated | Suffix | `manifest.json` | SQS Output Queue |
| xena-error | ObjectCreated | None | - | SQS Error Queue |

### EventBridge Events

| Rule | Schedule | Target |
|------|----------|--------|
| Reconciliation | rate(10 minutes) | ReconciliationJob Lambda |

### SQS Queues

| Queue | Source | Consumer | Batch Size |
|-------|--------|----------|------------|
| xena-output-queue | S3 Output Events | OutputHandler Lambda | 10 |
| xena-error-queue | S3 Error Events | ErrorHandler Lambda | 10 |
| xena-output-dlq | Failed messages | Manual review | - |
| xena-error-dlq | Failed messages | Manual review | - |

## 8.3 Lambda Invocation Patterns

### Direct Invocation

- **XENALoader** ← S3 Input Events (direct)
- Reason: Infrequent events, immediate processing desired

### SQS-Triggered Invocation

- **OutputHandler** ← SQS Output Queue
- **ErrorHandler** ← SQS Error Queue
- Reason: Frequent events, needs buffering, retry capabilities

### Scheduled Invocation

- **ReconciliationJob** ← EventBridge (every 10 min)
- Reason: Periodic health check

### Lambda-to-Lambda Invocation

- **XENALoader** → **GRIDConnector** (async invoke)
- **ReconciliationJob** → **GRIDConnector** (async invoke)
- Reason: Separation of concerns, retry isolation

---

# 9. Book Lifecycle State Machine

## 9.1 State Diagram with Reconciliation

```
UPLOADED
   ↓
QUEUED (in SQS)
   ↓
SUBMITTED (to GRID)
   ↓
PROCESSING (XDE executing)
   ↓
   ├─ Success Path ─────────────────┐
   │                                 ↓
   │                            COMPLETED
   │                                 ↓
   │                          (Removed from pending_books)
   │
   └─ Failure Path ─────────────────┐
                                     ↓
                                  STALLED
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
| - | Book uploaded to S3 | UPLOADED | S3 event fired |
| UPLOADED | XENALoader invoked | QUEUED | Lambda processing |
| QUEUED | GRIDConnector called | SUBMITTED | GRID API call |
| SUBMITTED | XDE started | PROCESSING | Deal execution begins |
| PROCESSING | manifest.json written | COMPLETED | OutputHandler updates Redis |
| COMPLETED | OutputHandler processed | - | Remove from pending_books |
| PROCESSING | No progress >10 min | STALLED | ReconciliationJob detects |
| STALLED | Reconciliation triggered | RECONCILIATION | GRIDConnector with reconciliation=true |
| RECONCILIATION | XDE restarted | PROCESSING | Skip existing, process missing |

## 9.3 Redis State Tracking

### Book Metadata

**Key:** `xena:book:{bookId}`

**Fields:**
```json
{
  "status": "PROCESSING",
  "totalDeals": 48178,
  "completedDeals": 0,
  "failedDeals": 0,
  "lastProgressTimestamp": "2026-03-16T10:05:00Z",
  "gridJobId": "grid-742912",
  "bookRetries": 0,
  "startTime": "2026-03-16T10:00:00Z",
  "inputFile": "s3://xena-input/fmtm/book.parquet",
  "jobType": "FMTM"
}
```

### Pending Books Set

**Key:** `xena:pending_books`

**Type:** Set

**Members:** `{bookId1, bookId2, bookId3, ...}`

**Lifecycle:**
- **Added:** When XENALoader submits job
- **Removed:** When OutputHandler processes manifest.json

---

# 10. Failure and Retry Semantics

## 10.1 Retry Parameters

### Deal-Level Retry (within XDE)

**Configuration:**
- `maxRetries = 3`
- `backoff = exponential`

**Formula:**
```cpp
retryDelay = baseDelayMs * std::pow(2, attempt)
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

**Trigger:** ReconciliationJob detects stalled book

**Action:** Submit reconciliation job

**Parameters:**
- `reconciliation: true`
- `bookRetries++`

## 10.2 Lambda Retry Configuration

### XENALoader

**Retry Attempts:** 3
**Retry Behavior:** AWS Lambda automatic retry with exponential backoff
**DLQ:** After 3 failures → xena-input-dlq

### GRIDConnector

**Retry Attempts:** 3
**Retry Behavior:** AWS Lambda automatic retry
**DLQ:** After 3 failures → xena-grid-dlq

### OutputHandler

**Retry Attempts:** 3 (SQS message visibility timeout)
**Retry Behavior:** SQS redelivery after visibility timeout
**DLQ:** After 3 failures → xena-output-dlq

### ErrorHandler

**Retry Attempts:** 3
**Retry Behavior:** SQS redelivery
**DLQ:** After 3 failures → xena-error-dlq

## 10.3 Failure Isolation

**Critical Principle:** Deal failures do NOT fail the entire book.

**Behavior:**
```cpp
for (auto& deal : batch.deals) {
    try {
        Result result = processDealWithRetry(deal);
        resultQueue.push(result);
    } catch (const std::exception& e) {
        // Catch exception, mark deal as failed
        Result failedResult{deal.dealId, false, e.what()};
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

### Automatic Trigger (EventBridge)

**Schedule:** Every 10 minutes

**Detection Logic:**
```python
# ReconciliationJob Lambda
pending_books = redis.smembers('xena:pending_books')

for book_id in pending_books:
    progress = download_progress_file(book_id)
    elapsed = now() - parse_timestamp(progress['timestamp'])
    
    if elapsed > 10 * 60:  # 10 minutes
        trigger_reconciliation(book_id)
```

### Manual Trigger

**Via API:**
```bash
aws lambda invoke \
  --function-name ReconciliationJob \
  --payload '{"bookId": "eFX"}' \
  response.json
```

## 11.3 Reconciliation Payload

**Changes from Standard Processing:**
```json
{
  "bookId": "eFX",
  "jobType": "FMTM",
  "inputFile": "s3://xena-input/fmtm/book.parquet",
  "totalDeals": 48178,
  "bookSize": 850000000,
  "reconciliation": true,    // ← Changed to true
  "bookRetries": 1           // ← Incremented
}
```

## 11.4 XDE Reconciliation Behavior

### Processing Flow

```cpp
if (job.reconciliation) {
    logger.info("Starting reconciliation for book {}, retry attempt {}",
                job.bookId, job.bookRetries);
    
    job.bookRetries++;  // Increment counter
    
    uint64_t skippedDeals = 0;
    uint64_t processingDeals = 0;
    
    // Stream through all deals
    while (reader.hasNext()) {
        Deal deal = reader.nextDeal();
        
        // Check S3 for existing result
        if (resultExistsInS3(deal.dealId)) {
            // Deal already processed successfully - skip
            processedDeals.fetch_add(1);
            completedDeals.fetch_add(1);
            skippedDeals++;
            continue;
        }
        
        // Deal missing or failed - process it
        dealQueue.push(deal);
        processingDeals++;
    }
    
    logger.info("Reconciliation: skipping {} existing deals, processing {} deals",
                skippedDeals, processingDeals);
}
```

### Performance Benefit

**Example:**
- Book has 100,000 deals
- 95,000 succeeded in first attempt
- 5,000 failed or missing

**Reconciliation:**
- Checks S3 for 100,000 deals (HEAD requests ~5ms each)
- Skips 95,000 deals (already done)
- Processes only 5,000 missing deals

**Recovery Time:**
- Without reconciliation: ~10 minutes (full book)
- With reconciliation: ~30 seconds (5% of deals)
- **20x faster recovery!**

## 11.5 S3 Idempotency Check Optimization

### Current Implementation

```cpp
bool resultExistsInS3(const std::string& dealId) const {
    const std::string key = job.outputPrefix + "/deal_results.parquet";
    
    try {
        auto response = s3Client.headObject(bucket, key);
        return response.isSuccess();
    } catch (...) {
        return false;
    }
}
```

### Optimized Implementation (Future)

**Maintain a completion manifest:**

File: `_completed_deals.json`

```json
{
  "completedDealIds": [
    "deal-001",
    "deal-002",
    ...
    "deal-047000"
  ],
  "count": 47000,
  "lastUpdate": "2026-03-16T10:15:00Z"
}
```

**Benefits:**
- Single S3 GET instead of 100K HEAD requests
- Load into memory hash set
- O(1) lookup per deal
- Massive performance improvement for large books

---

# 12. Performance Characteristics

## 12.1 Target Performance

**Throughput:** 833+ books/min sustained

**Latency:**
- XENA overhead: <5s (P95) for serverless orchestration
- GRID processing: ~450s for 48K deals (example)
- Total end-to-end: <8 minutes for typical book

**Cluster Utilization:** ~95% (vs ~60% in book-level model)

**Lambda Cold Start:** <1s (with provisioned concurrency)

**Progress Checkpoint Interval:** Every 10 minutes (MAX) or every 500 deals

## 12.2 Throughput Calculation

**Example Book:**
- Total deals: 48,178
- Processing rate: 145 deals/sec (measured)

**Calculation:**
```
GRID Processing Time = 48,178 deals ÷ 145 deals/sec = 332 seconds ≈ 5.5 minutes
```

**XENA Serverless Overhead:**
```
XENALoader execution:     ~500ms
GRIDConnector execution:  ~300ms
OutputHandler execution:  ~400ms
Total Lambda overhead:    ~1.2s
```

**End-to-End:**
```
GRID Processing:      332s
Lambda Overhead:      1.2s
Total:                333.2s ≈ 5.5 minutes
```

**Books per Minute (with 80 parallel GRID workers):**
```
60 seconds ÷ 333 seconds = 0.18 books/sec per worker
0.18 books/sec × 80 workers × 60 = 864 books/min
```

**Target:** 833 books/min (conservative estimate with safety margin)

## 12.3 Lambda Performance

### XENALoader

**Invocations per minute:** ~833 (one per book)
**Average duration:** 500ms (Parquet metadata read)
**Memory usage:** ~200 MB
**Cold start:** ~800ms (with provisioned concurrency: ~100ms)

### GRIDConnector

**Invocations per minute:** ~833 (one per book)
**Average duration:** 300ms (REST API call)
**Memory usage:** ~128 MB
**Cold start:** ~600ms (with provisioned concurrency: ~80ms)

### OutputHandler

**Invocations per minute:** ~1,666 (2 per book: progress + manifest)
**Average duration:** 400ms (Redis update)
**Memory usage:** ~256 MB
**Cold start:** ~700ms (with provisioned concurrency: ~90ms)

### ErrorHandler

**Invocations per minute:** Variable (depends on failure rate)
**Average duration:** 200ms
**Memory usage:** ~128 MB

### ReconciliationJob

**Invocations:** Every 10 minutes
**Average duration:** 5-30s (depends on pending books count)
**Memory usage:** ~384 MB

## 12.4 Cost Comparison

### Old Architecture (with microservices/containers)

**Monthly Costs:**
```
3× services @ $150/month = $450/month
Container infrastructure = $200/month
───────────────────────────────────
Total = $650/month
```

### New Architecture (serverless)

**Monthly Costs:**
```
Lambda invocations (2M/month @ $0.20/1M) = $0.40
Lambda compute (10,000 GB-sec @ $0.0000166667) = $0.17
S3 storage (500 GB @ $0.023/GB) = $11.50
S3 requests (5M @ $0.005/1K) = $25.00
SQS messages (3M @ $0.40/1M) = $1.20
Redis (cache.t3.medium) = $50/month
───────────────────────────────────
Total = $88.27/month
```

**Savings:** $650 - $88.27 = **$561.73/month (86% reduction)**

**Annual Savings:** $6,740.76/year

---

# 13. Progress Tracking

## 13.1 Progress File Structure

**File:** `s3://xena-output/{bookId}/_progress.json`

**Update Frequency:** Every 10 minutes (configurable) OR every 500 deals (configurable)

**Schema:**
```json
{
  "bookId": "eFX",
  "processedDeals": 52300,
  "totalDeals": 80000,
  "lastProcessingDeal": "deal-918273",
  "timestamp": "2026-03-16T12:30:00Z"
}
```

## 13.2 Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `bookId` | string | Book identifier |
| `processedDeals` | int | Deals processed so far (completed + failed) |
| `totalDeals` | int | Total number of deals in book |
| `lastProcessingDeal` | string | Most recently processed deal ID |
| `timestamp` | string | Last progress update timestamp (ISO 8601) |

## 13.3 Use Cases

### 1. Monitoring GRID Progress

**From XENA/OutputHandler:**
```python
def get_book_progress(book_id):
    # Fetch from S3
    progress = download_progress_file(book_id)
    
    # Calculate percentage
    pct = (progress['processedDeals'] / progress['totalDeals']) * 100
    
    return {
        'bookId': book_id,
        'progress': f"{pct:.1f}%",
        'processed': progress['processedDeals'],
        'total': progress['totalDeals'],
        'lastUpdate': progress['timestamp']
    }
```

### 2. Stall Detection

**ReconciliationJob Lambda:**
```python
def check_stalled_books():
    pending_books = redis.smembers('xena:pending_books')
    
    for book_id in pending_books:
        progress = download_progress_file(book_id)
        last_update = parse_timestamp(progress['timestamp'])
        now = datetime.utcnow()
        
        # If no update in 10+ minutes, mark stalled
        if (now - last_update).seconds > 600:
            mark_stalled(book_id)
            trigger_reconciliation(book_id)
```

### 3. ETA Calculation

**Formula:**
```python
def calculate_eta(progress):
    book = redis.hgetall(f"xena:book:{progress['bookId']}")
    start_time = parse_timestamp(book['startTime'])
    now = datetime.utcnow()
    
    elapsed_seconds = (now - start_time).total_seconds()
    processed = progress['processedDeals']
    total = progress['totalDeals']
    
    if processed > 0:
        rate = processed / elapsed_seconds
        remaining = total - processed
        eta_seconds = remaining / rate
        return eta_seconds
    
    return None
```

### 4. Performance Analysis

**Metrics:**
```python
# Calculate processing rate
elapsed = (parse_timestamp(progress['timestamp']) - 
           parse_timestamp(book['startTime'])).total_seconds()
rate = progress['processedDeals'] / elapsed

# Deals per second
print(f"Processing rate: {rate:.1f} deals/sec")

# Estimated completion
remaining = progress['totalDeals'] - progress['processedDeals']
eta = remaining / rate
print(f"ETA: {eta/60:.1f} minutes")
```

## 13.4 Progress Write Implementation (XDE)

```cpp
void XDEEngine::writeProgress() {
    const auto now = std::chrono::system_clock::now();
    
    json progress = {
        {"bookId", job.bookId},
        {"processedDeals", processedDeals.load()},
        {"totalDeals", totalDeals},
        {"lastProcessingDeal", lastProcessingDeal},
        {"timestamp", formatTimestamp(now)}
    };
    
    // Write to S3
    s3Client.putObject(
        job.outputPrefix + "/_progress.json",
        progress.dump()
    );
    
    lastProgressWrite = now;
    
    logger.info("Progress update: {}/{} deals ({}%)",
                processedDeals.load(),
                totalDeals,
                (processedDeals.load() * 100.0 / totalDeals));
}
```

## 13.5 Progress File Lifecycle

```
Job Start
    ↓
Initial progress write (processedDeals=0)
    ↓
Periodic updates (every 10 min or 500 deals)
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
| **Architecture** | Microservices + Containers | Serverless (Lambda only) |
| **Infrastructure** | Requires infrastructure (containers/orchestration) | AWS managed services |
| **GRID → XENA Communication** | Redis BLPOP (polling) | S3 Event (push) |
| **Payload Storage** | Redis (memory pressure) | S3 (scalable) |
| **Result Aggregation** | In-memory (bottleneck) | S3 (distributed) |
| **Completion Detection** | Redis polling | S3 manifest event (suffix filtering) |
| **Progress Tracking** | None | _progress.json (every 10 min) |
| **Reconciliation** | Full recompute | Partial recompute (stateless) |
| **Failure Isolation** | Book-level (all-or-nothing) | Deal-level (partial success) |
| **Memory Footprint** | Large (full books in Redis) | Small (metadata only) |
| **Redis Usage** | Data bus (anti-pattern) | Metadata only |
| **GRID Integration** | Redis writes | Direct S3 writes |
| **Event Filtering** | Lambda code | S3 suffix filter |
| **Streaming** | Full book load | Streaming Parquet reader |
| **Tail Latency** | High (wait for slowest book) | Low (incremental completion) |
| **Observability** | Limited | ETA, progress, metrics |
| **Operational Cost** | High ($650/month) | Low ($88/month) |
| **Cost Reduction** | Baseline | **86% reduction** |

## 14.2 Architecture Pattern Comparison

### MVP1 (Anti-Patterns)

```
❌ Microservices
❌ Containers/infrastructure
❌ Redis as Data Bus
❌ BLPOP Polling
❌ Book-Level Processing
❌ In-Memory Aggregation
❌ Lambda Filters in Code
❌ Stateful Reconciliation
```

### MVP2 (Best Practices)

```
✅ Serverless Functions
✅ AWS Managed Services
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

**Cost Reduction:**
```
MVP1: $650/month (microservices + infrastructure)
MVP2: $88/month (serverless)
Improvement: 86% cost reduction
```

## 14.4 Operational Improvements

**MVP1 Issues:**
- Frequent Redis memory alerts
- Service crashes
- BLPOP timeout issues
- Manual book retry processes
- Limited visibility into GRID progress
- Complex deployment (Kubernetes)

**MVP2 Solutions:**
- 90% Redis memory reduction
- No service management required
- Zero polling overhead
- Automatic reconciliation
- Real-time progress tracking via _progress.json
- Simple deployment (Terraform/CloudFormation)

---

# 15. Deployment Architecture

## 15.1 Infrastructure as Code

XENA MVP2 uses **Infrastructure as Code** for all resource provisioning.

### Deployment Tools

| Tool | Purpose |
|------|---------|
| **Terraform** | Primary IaC tool for AWS resources |
| **CloudFormation** | Alternative/backup IaC option |
| **AWS SAM** | Lambda-specific deployments |

### Resources Deployed

**Lambda Functions:**
- XENALoader
- GRIDConnector
- OutputHandler
- ErrorHandler
- ReconciliationJob

**SQS Queues:**
- xena-output-queue
- xena-error-queue
- xena-output-dlq
- xena-error-dlq

**S3 Buckets:**
- xena-input
- xena-output
- xena-error

**EventBridge Rules:**
- ReconciliationScheduler (rate: 10 minutes)

**Redis Cluster:**
- ElastiCache (cache.t3.medium)

**IAM Roles:**
- LambdaExecutionRole
- GridExecutionRole (cross-account)

## 15.2 Terraform Example

### Lambda Function

```hcl
resource "aws_lambda_function" "xena_loader" {
  function_name = "XENALoader"
  role          = aws_iam_role.lambda_execution.arn
  handler       = "index.lambda_handler"
  runtime       = "python3.11"
  timeout       = 30
  memory_size   = 512
  
  filename         = "xena_loader.zip"
  source_code_hash = filebase64sha256("xena_loader.zip")
  
  environment {
    variables = {
      GRID_API_URL = var.grid_api_url
      REDIS_ENDPOINT = aws_elasticache_cluster.xena_redis.cache_nodes[0].address
    }
  }
  
  reserved_concurrent_executions = 100
  
  dead_letter_config {
    target_arn = aws_sqs_queue.xena_input_dlq.arn
  }
}
```

### S3 Event Notification

```hcl
resource "aws_s3_bucket_notification" "xena_input" {
  bucket = aws_s3_bucket.xena_input.id
  
  lambda_function {
    lambda_function_arn = aws_lambda_function.xena_loader.arn
    events              = ["s3:ObjectCreated:*"]
    filter_prefix       = "fmtm/"
  }
  
  lambda_function {
    lambda_function_arn = aws_lambda_function.xena_loader.arn
    events              = ["s3:ObjectCreated:*"]
    filter_prefix       = "calibration/"
  }
}
```

### EventBridge Rule

```hcl
resource "aws_cloudwatch_event_rule" "reconciliation" {
  name                = "xena-reconciliation-scheduler"
  description         = "Triggers reconciliation job every 10 minutes"
  schedule_expression = "rate(10 minutes)"
}

resource "aws_cloudwatch_event_target" "reconciliation" {
  rule      = aws_cloudwatch_event_rule.reconciliation.name
  target_id = "ReconciliationJobLambda"
  arn       = aws_lambda_function.reconciliation_job.arn
}
```

## 15.3 Deployment Environments

### Development

```
Environment: dev
Lambda Memory: Reduced (256-512 MB)
Reserved Concurrency: None
Redis: cache.t3.micro
S3 Lifecycle: 7 days
```

### Staging

```
Environment: staging
Lambda Memory: Standard (512-1024 MB)
Reserved Concurrency: 50% of production
Redis: cache.t3.small
S3 Lifecycle: 14 days
```

### Production

```
Environment: prod
Lambda Memory: Optimized (512-1024 MB)
Reserved Concurrency: 100 (XENALoader), 50 (others)
Provisioned Concurrency: 10 (XENALoader, OutputHandler)
Redis: cache.t3.medium (Multi-AZ)
S3 Lifecycle: 30 days
```

## 15.4 CI/CD Pipeline

```
GitHub Commit
    ↓
GitHub Actions
    ↓ (Run tests)
Unit Tests (Python)
    ↓ (Package)
Lambda Deployment Packages (.zip)
    ↓ (Upload to S3)
Artifact Bucket
    ↓ (Terraform Apply)
AWS Infrastructure Update
    ↓ (Lambda Update)
Lambda Functions Updated
    ↓ (Verify)
Smoke Tests
    ↓ (Monitor)
CloudWatch Alarms
```

## 15.5 No Kubernetes Required

**Critical Change from MVP1:**

MVP1 required:
- Kubernetes cluster management
- Pod deployments
- Service configurations
- Ingress controllers
- ConfigMaps and Secrets
- Helm charts
- kubectl commands

MVP2 requires:
- **None of the above**
- Simple Lambda deployment
- Infrastructure managed by AWS
- No container orchestration
- No cluster management

**Operational Simplification:** Massive reduction in complexity and operational burden.

---

# 16. Monitoring and Observability

## 16.1 Key Metrics

### XENA Serverless Metrics

| Metric | Type | Source | Threshold |
|--------|------|--------|-----------|
| `lambda_invocations_total` | Counter | CloudWatch | - |
| `lambda_errors_total` | Counter | CloudWatch | Alert if >1% |
| `lambda_duration_seconds` | Histogram | CloudWatch | P95 <5s |
| `lambda_concurrent_executions` | Gauge | CloudWatch | Monitor capacity |
| `sqs_queue_depth` | Gauge | CloudWatch | Alert if >1000 |
| `sqs_messages_deleted` | Counter | CloudWatch | - |
| `books_submitted_total` | Counter | Custom Metric | - |
| `books_completed_total` | Counter | Custom Metric | - |
| `books_failed_total` | Counter | Custom Metric | Alert if >1% |
| `book_processing_duration_seconds` | Histogram | Custom Metric | P95 <600s |

### GRID Metrics (from _progress.json)

| Metric | Type | Source | Threshold |
|--------|------|--------|-----------|
| `deal_execution_time_seconds` | Histogram | _progress.json | P95 <0.5s |
| `processing_rate_deals_per_sec` | Gauge | _progress.json | >100 |
| `reconciliation_skip_count` | Counter | XDE logs | - |

### Infrastructure Metrics

| Metric | Type | Source | Threshold |
|--------|------|--------|-----------|
| `s3_request_rate` | Gauge | CloudWatch | - |
| `s3_error_rate` | Gauge | CloudWatch | Alert if >1% |
| `redis_cpu_utilization` | Gauge | CloudWatch | Alert if >80% |
| `redis_memory_utilization` | Gauge | CloudWatch | Alert if >80% |
| `redis_connections` | Gauge | CloudWatch | Monitor |

## 16.2 Alerting Rules

| Alert | Condition | Severity | Action |
|-------|-----------|----------|--------|
| High Lambda error rate | lambda_errors >1% | Critical | PagerDuty |
| SQS backlog | queue_depth >1000 | Warning | Email |
| Lambda timeout | duration >timeout | Warning | Email |
| Redis high memory | memory >80% | Warning | Email |
| S3 errors | s3_errors >1% | Critical | Email |
| Stalled books | No progress >10min | Critical | Auto-reconciliation |
| Lambda throttling | ThrottledCount >0 | Warning | Email + Scale |

## 16.3 Distributed Tracing

**Tool:** AWS X-Ray

**Traced Components:**
- Lambda functions (XENALoader, GRIDConnector, OutputHandler, ErrorHandler)
- S3 API calls
- SQS operations
- Redis operations
- GRID REST API calls

**Trace IDs:**
- Propagated via Lambda context
- Linked across function invocations
- End-to-end visibility from S3 event to completion

**Example Trace:**
```
Book Submission (trace-id: 1-2-3-4-5)
  ├─ S3 Event (3ms)
  ├─ XENALoader Lambda (450ms)
  │   ├─ S3 GetObject (50ms)
  │   ├─ Parquet metadata read (200ms)
  │   ├─ Redis write (20ms)
  │   └─ Invoke GRIDConnector (180ms)
  ├─ GRIDConnector Lambda (300ms)
  │   ├─ Build payload (10ms)
  │   ├─ GRID API call (250ms)
  │   └─ Redis update (40ms)
  └─ Total: 753ms
```

## 16.4 Logging

**Centralized Logging:** CloudWatch Logs

**Log Groups:**
- `/aws/lambda/XENALoader`
- `/aws/lambda/GRIDConnector`
- `/aws/lambda/OutputHandler`
- `/aws/lambda/ErrorHandler`
- `/aws/lambda/ReconciliationJob`

**Log Retention:** 30 days (production), 7 days (dev)

**Structured Logging:**
```json
{
  "timestamp": "2026-03-16T10:05:12Z",
  "level": "INFO",
  "lambda": "XENALoader",
  "bookId": "eFX",
  "jobType": "FMTM",
  "totalDeals": 48178,
  "message": "Job submitted to GRID",
  "traceId": "1-5f8a1b2c-3d4e5f6a7b8c9d0e1f2a3b4c",
  "requestId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
}
```

## 16.5 Dashboards

**CloudWatch Dashboard:** XENA MVP2 Production

**Panels:**
1. **Lambda Performance**
   - Invocations by function
   - Error rates
   - Duration percentiles (P50, P95, P99)
   - Concurrent executions

2. **SQS Queues**
   - Queue depths (Output, Error)
   - Messages sent/received/deleted
   - DLQ depths

3. **Book Processing**
   - Books submitted (rate)
   - Books completed (rate)
   - Books failed (count)
   - Processing duration distribution

4. **GRID Progress**
   - Deal processing rate (from _progress.json)
   - Active books (pending_books set size)
   - Stalled books count

5. **Infrastructure**
   - S3 request rates
   - Redis CPU/memory
   - Lambda throttles
   - Cost metrics

6. **End-to-End Latency**
   - Submission to completion time
   - Lambda cold starts
   - GRID API response time

---

# 17. Reliability Guarantees

## 17.1 Failure Scenarios and Outcomes

| Failure | Impact | Outcome | Mitigation |
|---------|--------|---------|------------|
| **Single deal failure** | Deal error record | Continue processing | Write to Error bucket, count in manifest |
| **Lambda failure** | Single invocation retry | Automatic retry | AWS Lambda retry (3x), then DLQ |
| **Lambda timeout** | Invocation fails | Automatic retry | Increase timeout or optimize code |
| **GRID worker crash** | Partial book delay | Queue redistribution | Remaining workers continue |
| **GRID job crash** | Book stalls | Reconciliation detects | Mark STALLED, auto-reconciliation |
| **S3 outage** | System halt | Wait for AWS recovery | No mitigation (AWS dependency) |
| **Redis failure** | Metadata loss | Rebuild from S3 | Replay S3 events, rebuild state |
| **SQS failure** | Message delivery delay | AWS managed retry | DLQ captures failed messages |
| **EventBridge failure** | Missed reconciliation | Next scheduled run | Minimal impact (runs every 10 min) |

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
- OutputHandler processes and updates Redis

### 4. External Observability

**Guarantee:** XENA can monitor GRID progress

**Achieved by:** `_progress.json` updated every 10 min with ETA and metrics

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
- XDE reconciliation (S3 result check)

**Example:**
```python
def lambda_handler(event, context):
    for record in event['Records']:
        message_id = record['messageId']
        
        # Check idempotency key
        if redis.exists(f"processed:{message_id}"):
            logger.info(f"Duplicate message {message_id}, skipping")
            continue
        
        # Process message
        process_manifest(record)
        
        # Mark as processed (TTL: 1 hour)
        redis.setex(f"processed:{message_id}", 3600, "1")
```

### 8. Serverless Auto-Scaling

**Guarantee:** System scales automatically with load

**Achieved by:**
- Lambda auto-scales to handle invocations
- SQS buffers messages during spikes
- S3 handles any write throughput
- No manual capacity planning required

## 17.3 SLA Targets

**Availability:** 99.9% (three nines)

**Throughput:** 833 books/min sustained

**Latency:**
- Lambda overhead: <5s (P95)
- Total end-to-end: <8 min for typical books

**Data Durability:** 99.999999999% (S3 guarantee)

**Error Rate:** <0.1% (book-level failures)

**Recovery Time:**
- Reconciliation: <10 min to detect stalled book
- Automatic retry: Triggered immediately upon detection

---

# 18. Architecture Principles

This section summarizes the core architectural principles that guide XENA MVP2's design.

## 18.1 Principles Summary

| Principle | How Applied |
|-----------|-------------|
| **Event-Driven Architecture** | S3 events trigger all processing; no polling |
| **Serverless Control Plane** | All orchestration via Lambda; no containers |
| **Shared Storage Contract** | S3 used as integration layer between systems |
| **Stateless Compute** | GRID handles processing; XENA orchestrates |
| **Idempotent Processing** | Reconciliation supported; duplicate-safe |
| **Horizontal Scalability** | Lambda + GRID scale independently |
| **Single Responsibility** | Each Lambda has one focused task |
| **Fail Fast, Recover Fast** | Deal-level isolation; automatic reconciliation |
| **Observability First** | Progress tracking, metrics, distributed tracing |
| **Cost Optimization** | Pay-per-use serverless; no idle resources |

## 18.2 Event-Driven Architecture

**Principle:** All system interactions are event-driven; no synchronous polling.

**Implementation:**
- S3 ObjectCreated events trigger Lambdas
- SQS decouples event production from consumption
- EventBridge schedules periodic tasks
- No BLPOP or polling mechanisms

**Benefits:**
- Zero idle resource consumption
- Natural backpressure handling
- Simplified error handling
- Clear audit trail

## 18.3 Serverless Control Plane

**Principle:** All orchestration runs on AWS-managed serverless services.

**Implementation:**
- Lambda functions for all business logic
- S3 for data storage
- SQS for message queuing
- EventBridge for scheduling
- Redis for metadata only (no compute)

**Benefits:**
- No infrastructure management
- Automatic scaling
- Pay-per-use pricing
- High availability by default

## 18.4 Shared Storage Contract

**Principle:** S3 is the single source of truth for all data exchange.

**Implementation:**
- GRID writes results directly to S3
- XENA reads from S3, not from GRID
- No in-memory data passing
- S3 events signal data availability

**Benefits:**
- Decoupled systems
- Data durability (99.999999999%)
- Audit trail (S3 access logs)
- Simplified integration

## 18.5 Stateless Compute

**Principle:** Compute engines (GRID/XDE) are stateless; state lives in S3/Redis.

**Implementation:**
- XDE processes deals without external state
- Reconciliation uses S3 for idempotency checks
- No database dependencies in compute path
- State stored in S3 (_progress.json, manifest.json)

**Benefits:**
- Easy horizontal scaling
- Simple failure recovery
- No state synchronization overhead
- Predictable behavior

## 18.6 Idempotent Processing

**Principle:** All operations can be safely retried without side effects.

**Implementation:**
- Lambda functions check Redis for duplicates
- XDE checks S3 for existing results during reconciliation
- SQS message deduplication
- Atomic S3 writes

**Benefits:**
- Safe automatic retries
- Resilient to network failures
- Simplified error handling
- Reconciliation without risk

## 18.7 Horizontal Scalability

**Principle:** System scales by adding more parallel units, not bigger ones.

**Implementation:**
- Lambda concurrency increases with load
- GRID workers scale independently
- Deal-level parallelism (not book-level)
- No central bottleneck

**Benefits:**
- Linear scaling characteristics
- No coordination overhead
- Cost scales with usage
- No capacity planning needed

## 18.8 Single Responsibility

**Principle:** Each component has one clear, focused responsibility.

**Implementation:**
- XENALoader: Handle S3 events, submit to GRID
- GRIDConnector: Call GRID API only
- OutputHandler: Process manifests only
- ErrorHandler: Handle errors only
- ReconciliationJob: Detect stalls only

**Benefits:**
- Simple to understand
- Easy to test
- Independent deployment
- Clear ownership

## 18.9 Fail Fast, Recover Fast

**Principle:** Detect failures quickly and recover automatically.

**Implementation:**
- Deal-level failure isolation
- Automatic reconciliation every 10 minutes
- DLQs for persistent failures
- Progress tracking for early detection

**Benefits:**
- Minimal impact radius
- Fast recovery (seconds to minutes)
- No manual intervention needed
- Graceful degradation

## 18.10 Observability First

**Principle:** System behavior is visible and measurable at all times.

**Implementation:**
- _progress.json for real-time status
- CloudWatch metrics for all components
- X-Ray distributed tracing
- Structured logging
- Custom dashboards

**Benefits:**
- Proactive problem detection
- Clear debugging path
- Performance optimization data
- Compliance and audit support

## 18.11 Cost Optimization

**Principle:** Minimize cost without sacrificing reliability or performance.

**Implementation:**
- Serverless pay-per-use model
- No idle resources
- Right-sized Lambda memory
- S3 lifecycle policies
- Reserved concurrency only where needed

**Benefits:**
- 86% cost reduction vs MVP1
- Automatic cost scaling with usage
- No overprovisioning waste
- Predictable pricing

---

# 19. Redis Metadata Model

## 19.1 Redis Keys

### Book Metadata

**Key Pattern:** `xena:book:{bookId}`

**Type:** Hash

**Fields:**
```json
{
  "status": "PROCESSING",
  "totalDeals": "48178",
  "completedDeals": "0",
  "failedDeals": "0",
  "lastProgressTimestamp": "2026-03-16T10:05:00Z",
  "gridJobId": "grid-742912",
  "bookRetries": "0",
  "startTime": "2026-03-16T10:00:00Z",
  "inputFile": "s3://xena-input/fmtm/book.parquet",
  "jobType": "FMTM"
}
```

### Pending Books Set

**Key:** `xena:pending_books`

**Type:** Set

**Members:** `{bookId1, bookId2, bookId3, ...}`

**Lifecycle:**
```
XENALoader submits job
    ↓
SADD xena:pending_books {bookId}
    ↓
(Book processing...)
    ↓
OutputHandler receives manifest.json
    ↓
SREM xena:pending_books {bookId}
```

### Idempotency Keys

**Key Pattern:** `processed:{messageId}`

**Type:** String

**TTL:** 1 hour

**Purpose:** Prevent duplicate Lambda processing

## 19.2 Redis Operations

### XENALoader

```python
# Register book
redis.hset(f"xena:book:{book_id}", mapping={
    'status': 'SUBMITTED',
    'totalDeals': total_deals,
    'completedDeals': 0,
    'failedDeals': 0,
    'startTime': datetime.utcnow().isoformat(),
    'inputFile': input_file,
    'jobType': job_type,
    'bookRetries': 0
})

# Add to pending set
redis.sadd('xena:pending_books', book_id)
```

### OutputHandler

```python
# Update progress
if file_type == '_progress.json':
    redis.hset(f"xena:book:{book_id}", mapping={
        'processedDeals': data['processedDeals'],
        'lastProgressTimestamp': data['timestamp']
    })

# Mark completed
if file_type == 'manifest.json':
    redis.hset(f"xena:book:{book_id}", mapping={
        'status': 'COMPLETED',
        'completedDeals': data['completedDeals'],
        'failedDeals': data['failedDeals'],
        'endTime': data['endTime']
    })
    
    # Remove from pending
    redis.srem('xena:pending_books', book_id)
```

### ReconciliationJob

```python
# Get all pending books
pending_books = redis.smembers('xena:pending_books')

# For each book, check if stalled
for book_id in pending_books:
    book = redis.hgetall(f"xena:book:{book_id}")
    
    # Download _progress.json, check timestamp
    # If stalled, mark and trigger reconciliation
    redis.hset(f"xena:book:{book_id}", 'status', 'STALLED')
```

## 19.3 Memory Usage

**Typical Book Metadata:** ~2 KB per book

**Example:**
- 1,000 concurrent books
- 2 KB per book
- Total: ~2 MB

**Redis Instance:** cache.t3.medium (3.09 GB memory)
- Metadata: ~2 MB
- Overhead: ~50 MB
- **Total Usage:** <1% of capacity

**Contrast with MVP1:**
- MVP1: Stored full payloads in Redis (850 MB per book)
- MVP2: Stores only metadata (~2 KB per book)
- **Reduction:** 99.9%

---

# Appendices

## Appendix A: Glossary

**Book:** A collection of deals representing a full Murex book export (Parquet format)

**Deal:** Individual financial instrument record within a book

**FMTM:** Full Market-To-Market valuation process

**XDE:** eXtended Data Engine - GRID's optimized C++ compute engine

**Manifest:** Authoritative completion signal file (`manifest.json`)

**Progress Marker:** Periodic status file (`_progress.json`) updated every 10 min or 500 deals

**Serverless:** Cloud computing execution model where infrastructure is fully managed by cloud provider

**Lambda:** AWS serverless compute service for running code without managing servers

**EventBridge:** AWS serverless event bus service for application integration

**Idempotency:** Property ensuring duplicate requests produce same result

**Cross-Account IAM:** AWS pattern where one account assumes a role in another

**Streaming:** Processing data incrementally without loading entirely into memory

**Reconciliation:** Processing mode that reuses existing S3 results to minimize recomputation

**Suffix Filtering:** S3 event configuration that triggers events only for files matching a specific suffix

## Appendix B: Acronyms

| Acronym | Full Term |
|---------|-----------|
| FMTM | Full Market-To-Market |
| XDE | eXtended Data Engine |
| MVP | Minimum Viable Product |
| IAM | Identity and Access Management |
| SQS | Simple Queue Service |
| DLQ | Dead Letter Queue |
| HPA | Horizontal Pod Autoscaler (obsolete in MVP2) |
| NLB | Network Load Balancer (obsolete in MVP2) |
| RTO | Recovery Time Objective |
| RPO | Recovery Point Objective |
| SLA | Service Level Agreement |
| TTL | Time To Live |
| SRP | Single Responsibility Principle |
| ETA | Estimated Time to Arrival/Completion |
| IaC | Infrastructure as Code |
| CI/CD | Continuous Integration / Continuous Deployment |

## Appendix C: References

**Internal Documents:**
- XENA MVP1 Architecture Documentation
- GRID Integration Proposal (March 2026)
- Cross-Account Security Standards
- XENA MVP2 FMTM Technical Specification v2.0
- Serverless Migration Decision Document (March 2026)

**External References:**
- AWS Lambda Documentation: https://docs.aws.amazon.com/lambda/
- AWS S3 Best Practices: https://docs.aws.amazon.com/s3/
- AWS S3 Event Notifications: https://docs.aws.amazon.com/AmazonS3/latest/userguide/NotificationHowTo.html
- AWS SQS Documentation: https://docs.aws.amazon.com/sqs/
- AWS EventBridge Documentation: https://docs.aws.amazon.com/eventbridge/
- Apache Parquet Documentation: https://parquet.apache.org/
- Apache Arrow C++: https://arrow.apache.org/docs/cpp/
- AWS Serverless Application Model: https://docs.aws.amazon.com/serverless-application-model/
- Terraform AWS Provider: https://registry.terraform.io/providers/hashicorp/aws/

---

# Document Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-03-05 | Architecture Team | Initial release |
| 2.0 | 2026-03-08 | Architecture Team | Added dual execution modes, reconciliation details, progress tracking, S3 suffix filtering clarification, XDE processing model expansion, component separation refinements |
| **3.0** | **2026-03-16** | **Architecture Team** | **Major revision: Removed microservices/containers, converted to fully serverless event-driven system with Lambda orchestration, added S3 event filtering, enhanced reconciliation, added Architecture Principles section** |

---

**Document Classification:** Confidential  
**Document Owner:** XENA Architecture Team  
**Next Review Date:** 2026-06-16

---

*End of Document*
