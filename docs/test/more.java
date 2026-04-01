import lombok.Builder;
import lombok.Data;

@Data
@Builder
public class JobRequest {

    private String jobType;
    private String uniqueKey;
    private String objectKey;
    private Metadata metadata;
    private String version;

    private String inputFile;
    private String resultsPath;
    private String errorPath;
}

-------

private String buildInputFile(String bucket, String objectKey) {
    return "s3://" + bucket + "/" + objectKey;
}

private String extractBasePath(String objectKey) {
    int lastSlash = objectKey.lastIndexOf("/");
    return objectKey.substring(0, lastSlash + 1);
}

private String buildResultsPath(String outputBucket, String basePath) {
    return "s3://" + outputBucket + "/" + basePath;
}

private String buildErrorPath(String errorBucket, String basePath) {
    return "s3://" + errorBucket + "/" + basePath;
}


----

  private void processValidFile(Metadata metadata,
                              String objectKey,
                              String jobType,
                              Context context) {

    String uniqueKey = LambdaUtil.generateUniqueKey(metadata);
    String hashedKey = LambdaUtil.getSafeKey(uniqueKey);

    String bucket = metadata.getBucket(); // or pass from earlier

    String basePath = extractBasePath(objectKey);

    String inputFile = buildInputFile(bucket, objectKey);
    String resultsPath = buildResultsPath(outputBucketName, basePath);
    String errorPath = buildErrorPath("xena-data-lake-error", basePath); // config later

    boolean acquired = redisService.tryAcquireLock(hashedKey);

    if (!acquired) {
        context.getLogger().log(
                "Duplicate detected. Skipping key=" + uniqueKey,
                LogLevel.WARN
        );
        return;
    }

    JobRequest request = JobRequest.builder()
            .jobType(jobType.toLowerCase())
            .uniqueKey(uniqueKey)
            .objectKey(objectKey)
            .metadata(metadata)
            .version(metadata.getVersion())
            .inputFile(inputFile)
            .resultsPath(resultsPath)
            .errorPath(errorPath)
            .build();

    context.getLogger().log(
            "Enqueuing jobType=" + jobType +
            ", uniqueKey=" + uniqueKey +
            ", inputFile=" + inputFile,
            LogLevel.INFO
    );

    queueService.enqueue(request);
}

-----

  @Override
public void enqueue(JobRequest request) {

    try {
        String body;

        switch (request.getJobType()) {

            case "fmtm":
                body = buildFmtmPayload(request);
                break;

            case "calibration":
                body = buildCalibrationPayload(request);
                break;

            default:
                throw new IllegalArgumentException("Unsupported jobType: " + request.getJobType());
        }

        SendMessageRequest message = SendMessageRequest.builder()
                .queueUrl(queueUrl)
                .messageBody(body)
                .build();

        sqsClient.sendMessage(message);

    } catch (Exception e) {
        throw new RuntimeException("Error sending message to queue", e);
    }
}


private String buildFmtmPayload(JobRequest r) throws Exception {

    Map<String, Object> payload = new HashMap<>();

    payload.put("jobId", r.getUniqueKey());
    payload.put("bookId", r.getMetadata().getBookId());
    payload.put("valuationDate", r.getMetadata().getValuationDate());

    payload.put("calibratedMarket", r.getResultsPath());
    payload.put("co", r.getMetadata().getCo());
    payload.put("currency", r.getMetadata().getCurrency());

    payload.put("inputFile", r.getInputFile());
    payload.put("resultsPath", r.getResultsPath());
    payload.put("errorPath", r.getErrorPath());

    payload.put("totalDeals", r.getMetadata().getTotalDeals());
    payload.put("bookSizeBytes", r.getMetadata().getBookSizeBytes());

    payload.put("reconciliation", false);
    payload.put("bookRetries", 0);
    payload.put("etaSeconds", 180);

    return objectMapper.writeValueAsString(payload);
}

private String buildCalibrationPayload(JobRequest r) throws Exception {

    Map<String, Object> payload = new HashMap<>();

    payload.put("jobId", r.getUniqueKey());
    payload.put("jobType", "calibration");

    payload.put("inputFile", r.getInputFile());
    payload.put("resultsPath", r.getResultsPath());
    payload.put("errorPath", r.getErrorPath());

    payload.put("metadata", r.getMetadata());

    return objectMapper.writeValueAsString(payload);
}

----
  
