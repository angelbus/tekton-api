public interface PayloadBuilder {

    String getJobType();

    String build(JobRequest request) throws Exception;
}



------
  public class FmtmPayloadBuilder implements PayloadBuilder {

    private final ObjectMapper objectMapper;

    public FmtmPayloadBuilder(ObjectMapper objectMapper) {
        this.objectMapper = objectMapper;
    }

    @Override
    public String getJobType() {
        return "fmtm";
    }

    @Override
    public String build(JobRequest r) throws Exception {

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
}



----
  public class CalibrationPayloadBuilder implements PayloadBuilder {

    private final ObjectMapper objectMapper;

    public CalibrationPayloadBuilder(ObjectMapper objectMapper) {
        this.objectMapper = objectMapper;
    }

    @Override
    public String getJobType() {
        return "calibration";
    }

    @Override
    public String build(JobRequest r) throws Exception {

        Map<String, Object> payload = new HashMap<>();

        payload.put("jobId", r.getUniqueKey());
        payload.put("jobType", "calibration");

        payload.put("inputFile", r.getInputFile());
        payload.put("resultsPath", r.getResultsPath());
        payload.put("errorPath", r.getErrorPath());

        payload.put("metadata", r.getMetadata());

        return objectMapper.writeValueAsString(payload);
    }
}



---
  private final Map<String, PayloadBuilder> builders;


public SqsQueueServiceImpl(LambdaConfig config) {

    this.sqsClient = SqsClient.builder()
            .region(Region.of(config.getAwsRegion()))
            .build();

    this.queueUrl = config.getGridConnectorQueueUrl();

    ObjectMapper objectMapper = new ObjectMapper();

    List<PayloadBuilder> builderList = List.of(
            new FmtmPayloadBuilder(objectMapper),
            new CalibrationPayloadBuilder(objectMapper)
    );

    this.builders = builderList.stream()
            .collect(Collectors.toMap(
                    b -> b.getJobType().toLowerCase(),
                    Function.identity()
            ));
}


----

  @Override
public void enqueue(JobRequest request) {

    try {
        String jobType = request.getJobType().toLowerCase();

        PayloadBuilder builder = builders.get(jobType);

        if (builder == null) {
            throw new IllegalArgumentException("Unsupported jobType: " + jobType);
        }

        String body = builder.build(request);

        SendMessageRequest message = SendMessageRequest.builder()
                .queueUrl(queueUrl)
                .messageBody(body)
                .build();

        sqsClient.sendMessage(message);

    } catch (Exception e) {
        throw new RuntimeException("Error sending message to queue", e);
    }
}
---

  OCP compliant

Add a new job type:

class RiskPayloadBuilder implements PayloadBuilder { ... }


👉 Register it → done
👉 No modification to existing logic


----
package com.santander.cib.xena.model;

public enum JobStatus {
    RECEIVED,
    VALIDATED,
    ENQUEUED,
    PROCESSING,
    COMPLETED,
    FAILED
}


---
package com.santander.cib.xena.model;

import java.time.Instant;

public class JobState {

    private String objectKey;
    private JobStatus status;
    private Instant timestamp;

    public JobState() {}

    public JobState(String objectKey, JobStatus status, Instant timestamp) {
        this.objectKey = objectKey;
        this.status = status;
        this.timestamp = timestamp;
    }

    public String getObjectKey() {
        return objectKey;
    }

    public JobStatus getStatus() {
        return status;
    }

    public Instant getTimestamp() {
        return timestamp;
    }

    public void setObjectKey(String objectKey) {
        this.objectKey = objectKey;
    }

    public void setStatus(JobStatus status) {
        this.status = status;
    }

    public void setTimestamp(Instant timestamp) {
        this.timestamp = timestamp;
    }
}

----
package com.santander.cib.xena.service;

import com.amazonaws.services.lambda.runtime.Context;
import com.santander.cib.xena.model.JobState;

public interface RedisService {

    void saveState(String key, JobState state, Context context);

}

----
package com.santander.cib.xena.service.impl;

import com.amazonaws.services.lambda.runtime.Context;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.santander.cib.xena.model.JobState;
import com.santander.cib.xena.service.RedisService;

import java.time.Duration;

public class RedisServiceImpl implements RedisService {

    private final RedisClient redisClient; // your existing client
    private final ObjectMapper objectMapper;

    public RedisServiceImpl(RedisClient redisClient) {
        this.redisClient = redisClient;
        this.objectMapper = new ObjectMapper();
    }

    @Override
    public void saveState(String key, JobState state, Context context) {

        try {
            String value = objectMapper.writeValueAsString(state);

            // Optional TTL (recommended)
            redisClient.set(key, value, Duration.ofHours(24));

        } catch (Exception e) {
            context.getLogger().log("Error storing state in Redis: " + e.getMessage());
            throw new RuntimeException("Failed to store job state", e);
        }
    }
}

----
@RequiredArgsConstructor
public class RedisServiceImpl implements ElasticCacheService {

    private final LambdaConfig config;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Override
    public boolean tryAcquireLock(String key, Context context) {

        final String TTL_SECONDS = config.getRedisLockTTLSeconds();

        try (Jedis jedis = config.getJedisPool().getResource()) {

            SetParams params = new SetParams()
                    .nx()
                    .ex(Integer.parseInt(TTL_SECONDS));

            String result = jedis.set(key, "LOCKED", params);

            context.getLogger().log(
                    "Acquired Idempotency Lock for key: " + key +
                    " from Redis: " + result,
                    LogLevel.INFO
            );

            return "OK".equals(result);

        } catch (Exception e) {
            context.getLogger().log(
                    "Error acquiring Idempotency Lock for key: " + key +
                    " from Redis: " + e.getMessage(),
                    LogLevel.ERROR
            );
            return false;
        }
    }

    // ✅ NEW METHOD (replaces save metadata)
    @Override
    public void saveState(String key, JobState state, Context context) {

        try (Jedis jedis = config.getJedisPool().getResource()) {

            String value = objectMapper.writeValueAsString(state);

            // TTL for state (you may want a different config)
            int ttlSeconds = Integer.parseInt(config.getRedisStateTTLSeconds());

            jedis.setex(key, ttlSeconds, value);

            context.getLogger().log(
                    "JobState saved to Redis. key=" + key +
                    ", status=" + state.getStatus(),
                    LogLevel.INFO
            );

        } catch (Exception e) {
            context.getLogger().log(
                    "Error saving JobState for key: " + key +
                    " to Redis: " + e.getMessage(),
                    LogLevel.ERROR
            );
        }
    }
}

---
private void processValidFile(Metadata metadata,
                              String objectKey,
                              Context context) {

    String uniqueKey = LambdaUtil.generateUniqueKey(metadata);
    String hashedKey = LambdaUtil.getSafeKey(uniqueKey);

    context.getLogger().log(
            "Initializing job state for key=" + uniqueKey
    );

    JobState state = new JobState(
            objectKey,
            JobStatus.RECEIVED,
            Instant.now()
    );

    redisService.saveState(hashedKey, state, context);

    context.getLogger().log(
            "State stored in Redis: key=" + uniqueKey +
            ", status=" + state.getStatus()
    );

    // Continue with enqueue logic...
}

---
<dependency>
    <groupId>com.fasterxml.jackson.datatype</groupId>
    <artifactId>jackson-datatype-jsr310</artifactId>
</dependency>

import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import com.fasterxml.jackson.databind.SerializationFeature;

private final ObjectMapper objectMapper = new ObjectMapper()
        .registerModule(new JavaTimeModule())
        .disable(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS);
