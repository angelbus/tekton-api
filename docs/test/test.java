import com.amazonaws.services.lambda.runtime.Context;
import com.amazonaws.services.lambda.runtime.RequestHandler;
import com.amazonaws.services.lambda.runtime.events.SQSEvent;
import com.amazonaws.services.s3.event.S3EventNotification;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.regions.Region;

@RequiredArgsConstructor
@Setter
public class LambdaHandler implements RequestHandler<SQSEvent, Void> {

    private final DateTimeFormatter formatter =
            DateTimeFormatter.ofPattern("dd-MM-yyyy_HH:mm:ss.SSS");

    private final S3Client s3Client;
    private final RedisService redisService;
    private final QueueService queueService;
    private String outputBucketName;
}


@Override
public Void handleRequest(SQSEvent event, Context context) {

    context.getLogger().log("Lambda started - processing SQS batch", LogLevel.INFO);

    if (event.getRecords() == null || event.getRecords().isEmpty()) {
        context.getLogger().log("No records found in SQS event", LogLevel.ERROR);
        return null;
    }

    context.getLogger().log("Total SQS messages received: " + event.getRecords().size(), LogLevel.INFO);

    for (SQSEvent.SQSMessage message : event.getRecords()) {

        try {
            context.getLogger().log("Processing SQS messageId: " + message.getMessageId(), LogLevel.INFO);

            String body = message.getBody();
            context.getLogger().log("Raw SQS body: " + body, LogLevel.DEBUG);

            S3EventNotification s3Event = S3EventNotification.parseJson(body);

            processS3Event(s3Event, context);

        } catch (Exception e) {
            context.getLogger().log(
                    "Error processing SQS messageId: " + message.getMessageId() +
                    " error: " + e.getMessage(),
                    LogLevel.ERROR
            );

            throw e; // critical for retry
        }
    }

    context.getLogger().log("Lambda finished processing batch", LogLevel.INFO);

    return null;
}


private void processS3Event(S3EventNotification s3Event, Context context) {

    if (s3Event.getRecords() == null || s3Event.getRecords().isEmpty()) {
        context.getLogger().log("No S3 records found", LogLevel.ERROR);
        return;
    }

    var record = s3Event.getRecords().get(0);

    String bucketName = record.getS3().getBucket().getName();
    String objectKey = record.getS3().getObject().getKey();

    context.getLogger().log(
            "Processing S3 object: " + bucketName + "/" + objectKey,
            LogLevel.INFO
    );

    String jobType = extractJobType(objectKey);

    context.getLogger().log("Detected jobType: " + jobType, LogLevel.INFO);

    FileMetaData fileMetaData =
            ParquetUtil.readFileMetaData(s3Client, bucketName, objectKey);

    Metadata metadata;

    try {
        metadata = ParquetUtil.extractMetadata(fileMetaData);
        context.getLogger().log("Parquet metadata extracted successfully", LogLevel.INFO);

    } catch (ParquetValidationException e) {
        context.getLogger().log("Parquet validation failed: " + e.getMessage(), LogLevel.ERROR);
        processInvalidFile(objectKey, context);
        return;
    }

    Set<String> columnNames =
            ParquetUtil.extractCalibrationColumnNames(fileMetaData);

    boolean isMetadataValid = isValidMetadataFields(metadata, context);
    boolean isColumnsValid = isValidCalibrationColumns(columnNames, context);

    if (isMetadataValid && isColumnsValid) {
        processValidFile(metadata, objectKey, jobType, context);
    } else {
        processInvalidFile(objectKey, context);
    }
}


private void processValidFile(Metadata metadata,
                              String objectKey,
                              String jobType,
                              Context context) {

    String uniqueKey = LambdaUtil.generateUniqueKey(metadata);
    String hashedKey = LambdaUtil.getSafeKey(uniqueKey);

    context.getLogger().log(
            "Generated uniqueKey=" + uniqueKey + ", hashedKey=" + hashedKey,
            LogLevel.INFO
    );

    // 🔴 IDEMPOTENCY CHECK
    boolean acquired = redisService.tryAcquireLock(hashedKey);

    if (!acquired) {
        context.getLogger().log(
                "Duplicate detected. Skipping processing for key=" + uniqueKey,
                LogLevel.WARN
        );
        return;
    }

    context.getLogger().log(
            "Lock acquired. Proceeding with processing for key=" + uniqueKey,
            LogLevel.INFO
    );

    // Save metadata (optional depending on your design)
    redisService.save(metadata, hashedKey, context);

    context.getLogger().log(
            "Metadata stored in Redis for key=" + uniqueKey,
            LogLevel.INFO
    );

    // Enqueue job
    queueService.enqueue(
            JobRequest.builder()
                    .jobType(jobType)
                    .uniqueKey(uniqueKey)
                    .objectKey(objectKey)
                    .metadata(metadata)
                    .build()
    );

    context.getLogger().log(
            "Job successfully enqueued for key=" + uniqueKey +
            ", jobType=" + jobType +
            ", path=" + objectKey,
            LogLevel.INFO
    );
}



public interface RedisService {

    boolean tryAcquireLock(String key);

    void save(Metadata metadata, String key, Context context);
}


@Override
public boolean tryAcquireLock(String key) {

    // SETNX key value EX 3600
    // returns true if key was set (lock acquired)

    return redisClient.setIfAbsent(key, "LOCKED", Duration.ofHours(1));
}



private String extractJobType(String objectKey) {

    String[] parts = objectKey.split("/");

    if (parts.length < 4) {
        throw new IllegalArgumentException("Invalid S3 key structure: " + objectKey);
    }

    return parts[3]; // jobType
}


public interface QueueService {

    void enqueue(JobRequest request);

}


public class SqsQueueServiceImpl implements QueueService {

    private final SqsClient sqsClient;
    private final String queueUrl;
    private final ObjectMapper objectMapper;

    public SqsQueueServiceImpl(LambdaConfig config) {

        this.sqsClient = SqsClient.builder()
                .region(Region.of(config.getAwsRegion()))
                .build();

        this.queueUrl = config.getGridConnectorQueueUrl();
        this.objectMapper = new ObjectMapper();
    }

    @Override
    public void enqueue(JobRequest request) {

        try {
            String body = objectMapper.writeValueAsString(request);

            SendMessageRequest message = SendMessageRequest.builder()
                    .queueUrl(queueUrl)
                    .messageBody(body)
                    .build();

            sqsClient.sendMessage(message);

        } catch (Exception e) {
            throw new RuntimeException("Error sending message to queue", e);
        }
    }
}


import lombok.Builder;
import lombok.Data;

@Data
@Builder
public class JobRequest {

    private String jobType;
    private String uniqueKey;
    private String objectKey;
    private Metadata metadata;

}


