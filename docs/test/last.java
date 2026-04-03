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
