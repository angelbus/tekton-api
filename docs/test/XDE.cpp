/*
Streaming Parquet Reader (Chunked)

Requirement:

❌ Never deserialize entire book
✔ Stream records
  */
void streamDealsFromParquet()
{
    ParquetReader reader(job.inputFile);

    while(reader.hasNext())
    {
        DealBatch batch;

        size_t batchSize = computeBatchSize();

        for(size_t i=0;i<batchSize && reader.hasNext();i++)
        {
            Deal d = reader.nextDeal();

            if(job.reconciliation)
            {
                if(resultExistsInS3(d.dealId))
                    continue;
            }

            batch.deals.push_back(d);

            totalDeals++;
        }

        dealQueue.push(std::move(batch));
    }
}

//Dynamic Batch Size
size_t computeBatchSize()
{
    uint64_t remaining = totalDeals - processedDeals;

    if(remaining > 20000) return 200;
    if(remaining > 5000) return 50;
    if(remaining > 1000) return 10;

    return 1;
}

//Worker Thread Pool
void workerThread()
{
    DealBatch batch;

    while(dealQueue.pop(batch))
    {
        for(auto& deal : batch.deals)
        {
            Result r;

            try
            {
                r = processDealWithRetry(deal);
            }
            catch(...)
            {
                r.dealId = deal.dealId;
                r.success = false;
            }

            resultQueue.push(std::move(r));
        }
    }
}

//Retry Logic with Exponential Backoff
Result processDealWithRetry(Deal& deal)
{
    int retries = 0;

    int maxRetries = 3;

    while(true)
    {
        try
        {
            return computeFMTM(deal);
        }
        catch(...)
        {
            if(retries >= maxRetries)
                throw;

            int delay = pow(2, retries) * 100;

            sleep(delay);

            retries++;
        }
    }
}

//Aggregator Thread
void aggregatorThread()
{
    while(processedDeals < totalDeals)
    {
        Result r;

        resultQueue.pop(r);

        processedDeals++;

        if(r.success)
            completedDeals++;
        else
            failedDeals++;

        lastProcessingDeal = r.dealId;

        writeQueue.push(std::move(r));

        if(shouldWriteProgress())
            writeProgress();
    }
}
