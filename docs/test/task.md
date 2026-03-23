
| Task Name                                       | MVP | Description                                                                                     | Area     | Domain    |
| ----------------------------------------------- | --- | ----------------------------------------------------------------------------------------------- | -------- | --------- |
| Define Serverless Architecture Baseline         | 2   | Formalize removal of EKS and define Lambda + SQS + S3 + EventBridge architecture                | Solution | XENA      |
| Remove EKS Dependencies from Design             | 2   | Eliminate all Kubernetes concepts (Pods, Services, Ingress) from architecture and documentation | Solution | XENA      |
| Create S3 Input Bucket Structure                | 2   | Define `xena-input/calibration/` and `xena-input/fmtm/` prefixes and enforce naming conventions | Infra    | XENA      |
| Configure S3 Event Notifications (Input)        | 2   | Configure prefix-based triggers (calibration/, fmtm/) to invoke XENALoader Lambda               | Infra    | XENA      |
| Implement XENALoader Lambda                     | 2   | Parse S3 events, extract metadata, register job in Redis, build payload                         | Solution | XENA      |
| Extract Parquet Metadata (Footer Read)          | 2   | Implement efficient extraction of `totalDeals` without full file scan                           | Solution | XENA      |
| Create SQS Job Queue                            | 2   | Create `xena-grid-job-queue` to decouple ingestion from GRID submission                         | Infra    | XENA      |
| Configure SQS DLQ                               | 2   | Configure dead-letter queue for failed GRID submissions                                         | Infra    | XENA      |
| Enqueue Job Payload from XENALoader             | 2   | Push fully defined payload (jobType, totalDeals, etc.) into SQS                                 | Solution | XENA      |
| Define GRID Payload Contract (v1.0)             | 2   | Formalize payload schema including `jobType`, `gridOperation`, `reconciliation`, `bookRetries`  | Solution | XENA/GRID |
| Implement GRIDConnector Lambda                  | 2   | Consume SQS messages and invoke GRID REST API                                                   | Solution | XENA      |
| Implement GRID Endpoint Routing                 | 2   | Route requests based on `jobType` or `gridOperation` (FMTM vs Calibration)                      | Solution | XENA      |
| Configure Lambda Concurrency Limits             | 2   | Set reserved concurrency on GRIDConnector to throttle GRID submissions                          | Infra    | XENA      |
| Implement Temporary BLPOP Java Lambda           | 2   | Port existing SpringBoot BLPOP logic into a Java Lambda to support legacy GRID queue model      | Solution | XENA      |
| Integrate BLPOP Lambda with GRID                | 2   | Ensure Lambda can poll GRID queue and trigger processing (temporary bridge)                     | Solution | XENA/GRID |
| Define Migration Strategy (BLPOP → S3)          | 2   | Document transition from queue-based ingestion to shared storage model                          | Solution | XENA/GRID |
| Implement XDE Input via Shared Storage          | 2   | Enable XDE to read directly from S3 input path instead of queue (future state)                  | Solution | GRID      |
| Implement XDE Streaming Parquet Reader          | 2   | Read Parquet using row-group streaming (no full memory load)                                    | Solution | GRID      |
| Implement Deal Decomposition Logic              | 2   | Parse JSON deals inside Parquet and push to internal queues                                     | Solution | GRID      |
| Implement Global In-Memory Deal Queue           | 2   | Shared queue for worker threads to enable HTC distribution                                      | Solution | GRID      |
| Implement Worker Thread Pool                    | 2   | Create CPU-bound worker pool for deal processing                                                | Solution | GRID      |
| Implement Result Aggregation Logic              | 2   | Aggregate results and manage success/failure counts                                             | Solution | GRID      |
| Implement Async S3 Writer                       | 2   | Upload deal results and error outputs to respective buckets                                     | Solution | GRID      |
| Implement _progress.json Writer                 | 2   | Periodically write progress file (every ~10 minutes) with lastProcessingDeal                    | Solution | GRID      |
| Define _progress.json Contract                  | 2   | Include processedDeals, timestamp, lastProcessingDeal, retries                                  | Solution | GRID/XENA |
| Implement manifest.json Writer                  | 2   | Write final manifest only after all deals processed                                             | Solution | GRID      |
| Define manifest.json Contract                   | 2   | Include full metadata (counts, timestamps, paths, status)                                       | Solution | GRID/XENA |
| Create S3 Output Bucket Structure               | 2   | Define `xena-output/{bookId}/` with results, progress, manifest                                 | Infra    | XENA      |
| Create S3 Error Bucket Structure                | 2   | Define `xena-error/{bookId}/` for failed deals                                                  | Infra    | XENA      |
| Configure S3 Event Notifications (Output)       | 2   | Trigger only on `_progress.json` and `manifest.json` via suffix filter                          | Infra    | XENA      |
| Create SQS Output Queue                         | 2   | Route output events to SQS to avoid Lambda storms                                               | Infra    | XENA      |
| Implement OutputHandler Lambda                  | 2   | Process `_progress.json` and `manifest.json`, update Redis                                      | Solution | XENA      |
| Implement ErrorHandler Lambda                   | 2   | Process error bucket events and update failure metrics                                          | Solution | XENA      |
| Define Redis Metadata Model                     | 2   | Store job status, counts, timestamps, retries, gridJobId                                        | Solution | XENA      |
| Implement Redis Job Registration                | 2   | Add book to `pending_books` set and initialize metadata                                         | Solution | XENA      |
| Implement Completion Logic (Manifest-based)     | 2   | Remove book from pending list when manifest.json is received                                    | Solution | XENA      |
| Implement Idempotency Checks                    | 2   | Avoid duplicate job submission using Redis keys                                                 | Solution | XENA      |
| Implement Reconciliation Lambda                 | 2   | Detect stalled jobs based on `_progress.json` timestamp                                         | Solution | XENA      |
| Configure EventBridge Scheduler                 | 2   | Trigger reconciliation every 10 minutes                                                         | Infra    | XENA      |
| Implement Reconciliation Logic                  | 2   | Re-trigger GRIDConnector with `reconciliation=true`                                             | Solution | XENA      |
| Implement Retry & Backoff Strategy              | 2   | Add exponential backoff for GRID submission failures                                            | Solution | XENA      |
| Add Observability (CloudWatch + Metrics)        | 2   | Log job lifecycle, failures, throughput                                                         | Infra    | XENA      |
| Add Payload Versioning                          | 2   | Include `payloadVersion` field for forward compatibility                                        | Solution | XENA      |
| Validate End-to-End Flow (S3 → GRID → Manifest) | 2   | Perform integration testing across all components                                               | Solution | XENA/GRID |
| Performance Test (HTC Throughput)               | 2   | Validate 833 deals/min and scaling behavior                                                     | Solution | GRID      |
| Validate Tail Problem Mitigation                | 2   | Ensure balanced processing across workers                                                       | Solution | GRID      |
| Validate Reconciliation Scenarios               | 2   | Simulate failures and confirm recovery logic                                                    | Solution | XENA      |
| Remove BLPOP Dependency (Final State)           | 2   | Eliminate queue-based ingestion once GRID supports S3 fully                                     | Solution | XENA/GRID |
| Simplify GRIDConnector (REST Only)              | 2   | Remove legacy logic and keep only REST API integration                                          | Solution | XENA      |
| Decommission EKS Infrastructure                 | 2   | Remove Kubernetes cluster and related resources after validation                                | Infra    | XENA      |
