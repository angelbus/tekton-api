import boto3
import redis
import time
import io
import os
import json
from boto3.s3.transfer import TransferConfig
from concurrent.futures import ThreadPoolExecutor, as_completed
import concurrent.futures
from botocore.config import Config


# Configuration from environment variables
REDIS_HOST = os.environ.get('REDIS_HOST')  # benchmark-redis.xxxxx.cache.amazonaws.com
REDIS_PORT = int(os.environ.get('REDIS_PORT', 6379))
S3_BUCKET = os.environ.get('BUCKET_NAME')    # s3-vs-redis-benchmark-XXXXXXXX
S3_REGION = os.environ.get('AWS_REGION', 'us-east-1')

# Test configuration
FILE_SIZE_MB = 256
FILE_SIZE_BYTES = FILE_SIZE_MB * 1024 * 1024
CHUNK_SIZE_MB = 10
CHUNK_SIZE_BYTES = CHUNK_SIZE_MB * 1024 * 1024

# Initialize clients
s3_client = boto3.client('s3', region_name=S3_REGION)

# S3 client with Transfer Acceleration enabled
s3_accelerated = boto3.client(
    's3',
    region_name=S3_REGION,
    config=boto3.session.Config(
        s3={'use_accelerate_endpoint': True}
    )
)

redis_client = redis.Redis(
    host=REDIS_HOST,
    port=REDIS_PORT,
    decode_responses=False,
    socket_connect_timeout=10,
    socket_timeout=300
)

# Transfer configuration for multipart
transfer_config = TransferConfig(
    multipart_threshold=50 * 1024 * 1024,   # 50MB threshold
    multipart_chunksize=CHUNK_SIZE_BYTES,   # 10MB chunks
    max_concurrency=10,                      # 10 parallel uploads
    use_threads=True
)


def log_result(step, operation, duration, size_mb=FILE_SIZE_MB):
    """Log benchmark result"""
    throughput = size_mb / duration if duration > 0 else 0
    result = {
        'step': step,
        'operation': operation,
        'duration_seconds': round(duration, 3),
        'size_mb': size_mb,
        'throughput_mbps': round(throughput, 2)
    }
    print(json.dumps(result))
    return result


def generate_test_data(size_bytes):
    """Generate random test data"""
    print(f"Generating {size_bytes / 1024 / 1024:.0f}MB test data...")
    import random
    return bytes(random.getrandbits(8) for _ in range(size_bytes))


def test_redis_upload(test_data):
    """Step 1: Redis Upload (SET)"""
    print("\n--- Step 1: Redis Upload ---")
    key = 'benchmark:test_key'
    
    start = time.time()
    redis_client.set(key, test_data)
    duration = time.time() - start
    
    return log_result(1, 'Redis Upload (SET)', duration)


def test_redis_download():
    """Step 2: Redis Download (GET)"""
    print("\n--- Step 2: Redis Download ---")
    key = 'benchmark:test_key'
    
    start = time.time()
    data = redis_client.get(key)
    duration = time.time() - start
    
    if not data:
        raise Exception("Redis key not found!")
    
    print(f"Retrieved {len(data) / 1024 / 1024:.0f}MB from Redis")
    return log_result(2, 'Redis Download (GET)', duration)


def test_redis_cleanup():
    """Step 3: Redis Cleanup"""
    print("\n--- Step 3: Redis Cleanup ---")
    key = 'benchmark:test_key'
    
    redis_client.delete(key)
    print("Redis key deleted")


def test_s3_single_upload(test_data):
    """Step 4: S3 Single Upload (PutObject)"""
    print("\n--- Step 4: S3 Single Upload (PutObject) ---")
    key = 'benchmark/test_file_single.bin'
    
    start = time.time()
    s3_client.put_object(
        Bucket=S3_BUCKET,
        Key=key,
        Body=test_data
    )
    duration = time.time() - start
    
    print(f"Uploaded {FILE_SIZE_MB}MB via single PutObject")
    return log_result(4, 'S3 Single Upload (PutObject)', duration)


def test_s3_single_download():
    """Step 5: S3 Single Download (GetObject)"""
    print("\n--- Step 5: S3 Single Download (GetObject) ---")
    key = 'benchmark/test_file_single.bin'
    
    start = time.time()
    response = s3_client.get_object(
        Bucket=S3_BUCKET,
        Key=key
    )
    data = response['Body'].read()
    duration = time.time() - start
    
    print(f"Retrieved {len(data) / 1024 / 1024:.0f}MB from S3")
    return log_result(5, 'S3 Single Download (GetObject)', duration)


def test_s3_single_cleanup():
    """Step 6: S3 Cleanup (single file)"""
    print("\n--- Step 6: S3 Cleanup (Single File) ---")
    key = 'benchmark/test_file_single.bin'
    
    s3_client.delete_object(Bucket=S3_BUCKET, Key=key)
    print("S3 single file deleted")


def test_s3_managed_multipart_upload(test_data):
    """Step 7: S3 Managed Multipart Upload (boto3 automatic)"""
    print("\n--- Step 7: S3 Managed Multipart Upload ---")
    key = 'benchmark/test_file_multipart.bin'

    file_obj = io.BytesIO(test_data)
    file_obj.seek(0)

    print(f"Upload config: {CHUNK_SIZE_MB}MB chunks, 10 parallel threads")

    start = time.time()
    s3_client.upload_fileobj(
        file_obj,
        S3_BUCKET,
        key,
        Config=transfer_config
    )
    duration = time.time() - start

    num_parts = (len(test_data) + CHUNK_SIZE_BYTES - 1) // CHUNK_SIZE_BYTES
    print(f"boto3 automatically uploaded {num_parts} parts in parallel")
    
    return log_result(7, 'S3 Managed Multipart Upload (boto3)', duration)


def test_s3_managed_download():
    """Step 8: S3 Managed Download (boto3 automatic)"""
    print("\n--- Step 8: S3 Managed Download (boto3) ---")
    key = 'benchmark/test_file_multipart.bin'
    
    file_obj = io.BytesIO()
    
    start = time.time()
    s3_client.download_fileobj(
        S3_BUCKET,
        key,
        file_obj,
        Config=transfer_config
    )
    duration = time.time() - start
    
    data = file_obj.getvalue()
    print(f"boto3 downloaded {len(data) / 1024 / 1024:.0f}MB")
    
    return log_result(8, 'S3 Managed Download (boto3)', duration)


def test_s3_streaming_download():
    """Step 9: S3 Streaming Download (true stream, no buffering)"""
    print("\n--- Step 9: S3 Streaming Download ---")
    key = 'benchmark/test_file_multipart.bin'

    chunk_size = 1024 * 1024  # 1MB
    total_bytes = 0

    start = time.time()

    response = s3_client.get_object(
        Bucket=S3_BUCKET,
        Key=key
    )

    duration = time.time() - start
    body = response["Body"]

    while True:
        chunk = body.read(chunk_size)  # <-- TRUE streaming read
        if not chunk:
            break
        total_bytes += len(chunk)
        # process in chucks sequentially without accumulating in memory
        # do NOT accumulate chunk anywhere

    print(
        f"Streamed {total_bytes / 1024 / 1024:.0f}MB "
        f"in {chunk_size / 1024 / 1024:.0f}MB chunks"
    )
    return log_result(9, 'S3 Streaming Download', duration)

def test_s3_multipart_download():
    print("\n--- Step 9: S3 Multipart Download (Ranged GET) ---")
    key = 'benchmark/test_file_multipart.bin'
    part_size=32 * 1024 * 1024
    
    ranges = []
    offset = 0
    while offset < FILE_SIZE_BYTES:
        end = min(offset + part_size - 1, FILE_SIZE_BYTES - 1)
        ranges.append((offset, end))
        offset += part_size

    def download_range(r):
        resp = s3_client.get_object(
            Bucket=S3_BUCKET,
            Key=key,
            Range=f"bytes={r[0]}-{r[1]}"
        )
        return len(resp["Body"].read())

    total = 0
    start = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        for size in ex.map(download_range, ranges):
            total += size

    duration = time.time() - start
    return log_result(10, "S3 Multipart Download (Ranged GET)", duration)


def test_s3_range_download():
    """Step 10: S3 Range GET (Parallel Download with byte ranges)"""
    print("\n--- Step 10: S3 Range GET (Parallel Download) ---")
    key = 'benchmark/test_file_multipart.bin'
    
    # Get object size
    head = s3_client.head_object(Bucket=S3_BUCKET, Key=key)
    total_size = head['ContentLength']
    
    num_chunks = (total_size + CHUNK_SIZE_BYTES - 1) // CHUNK_SIZE_BYTES
    print(f"Downloading {total_size / 1024 / 1024:.0f}MB in {num_chunks} parallel {CHUNK_SIZE_MB}MB chunks...")
    
    chunks = [None] * num_chunks
    
    start = time.time()

    def download_chunk(chunk_index, start_byte, end_byte):
        """Download a single byte range"""
        response = s3_client.get_object(
            Bucket=S3_BUCKET,
            Key=key,
            Range=f'bytes={start_byte}-{end_byte}'
        )
        return chunk_index, response['Body'].read()
    
    # Download chunks in parallel
    with ThreadPoolExecutor(max_workers=10) as executor:
        futures = []
        
        for i in range(num_chunks):
            start_byte = i * CHUNK_SIZE_BYTES
            end_byte = min((i + 1) * CHUNK_SIZE_BYTES - 1, total_size - 1)
            
            future = executor.submit(download_chunk, i, start_byte, end_byte)
            futures.append(future)
        
        for future in as_completed(futures):
            chunk_index, chunk_data = future.result()
            chunks[chunk_index] = chunk_data
    
    duration = time.time() - start
    # Reassemble data
    full_data = b''.join(chunks)
    
    print(f"Downloaded {len(full_data) / 1024 / 1024:.0f}MB in {num_chunks} parallel chunks")
    return log_result(11, 'S3 Range GET (Parallel)', duration)


def test_s3_multipart_cleanup():
    """Step 11: S3 Cleanup (multipart file)"""
    print("\n--- Step 11: S3 Cleanup (Multipart File) ---")
    key = 'benchmark/test_file_multipart.bin'
    
    s3_client.delete_object(Bucket=S3_BUCKET, Key=key)
    print("S3 multipart file deleted")


def test_s3_accelerated_upload(test_data):
    """Step 12: S3 Transfer Acceleration Upload"""
    print("\n--- Step 12: S3 Transfer Acceleration Upload ---")

    accel_s3 = boto3.client(
        "s3",
        config=Config(s3={"use_accelerate_endpoint": True})
    )

    key = "benchmark/test_file_accelerated.bin"

    start = time.time()
    try:
        accel_s3.put_object(
            Bucket=S3_BUCKET,
            Key=key,
            Body=test_data
        )
    except Exception as e:
        print("⚠️ Transfer Acceleration failed (expected in VPC):", str(e))
        return {
            "step": 12,
            "name": "S3 Transfer Acceleration Upload",
            "status": "FAILED (not supported in VPC)",
            "duration_ms": None
        }

    duration = time.time() - start
    return log_result(12, "S3 Transfer Acceleration Upload", duration)


def test_s3_accelerated_download():
    """Step 13: S3 Transfer Acceleration Download"""
    print("\n--- Step 13: S3 Transfer Acceleration Download ---")
    key = 'benchmark/test_file_accelerated.bin'
    
    file_obj = io.BytesIO()
    
    print(f"Using S3 Transfer Acceleration endpoint")
    
    start = time.time()
    s3_accelerated.download_fileobj(
        S3_BUCKET,
        key,
        file_obj,
        Config=transfer_config
    )
    duration = time.time() - start
    
    data = file_obj.getvalue()
    print(f"Downloaded {len(data) / 1024 / 1024:.0f}MB via Transfer Acceleration")
    
    return log_result(13, 'S3 Transfer Acceleration Download', duration)


def test_s3_accelerated_cleanup():
    """Step 14: S3 Cleanup (accelerated file)"""
    print("\n--- Step 14: S3 Cleanup (Accelerated File) ---")
    key = 'benchmark/test_file_accelerated.bin'
    
    s3_client.delete_object(Bucket=S3_BUCKET, Key=key)
    print("S3 accelerated file deleted")


def lambda_handler(event, context):
    """Main Lambda handler"""
    print("=" * 80)
    print(f"REDIS vs S3 BENCHMARK - {FILE_SIZE_MB}MB File Transfer")
    print("=" * 80)
    print(f"S3 Bucket: {S3_BUCKET}")
    print(f"Redis Host: {REDIS_HOST}")
    print(f"Using boto3 managed transfers with {CHUNK_SIZE_MB}MB chunks")
    
    results = []
    
    try:
        # Generate test data once
        print("\nGenerating test data...")
        test_data = generate_test_data(FILE_SIZE_BYTES)
        print(f"Generated {len(test_data) / 1024 / 1024:.0f}MB test data")
        
        # REDIS TESTS
        results.append(test_redis_upload(test_data))
        results.append(test_redis_download())
        test_redis_cleanup()
        
        # S3 SINGLE UPLOAD/DOWNLOAD (no multipart)
        results.append(test_s3_single_upload(test_data))
        results.append(test_s3_single_download())
        test_s3_single_cleanup()
        
        # S3 MANAGED MULTIPART (boto3 automatic)
        results.append(test_s3_managed_multipart_upload(test_data))
        results.append(test_s3_managed_download())
        results.append(test_s3_streaming_download())
        results.append(test_s3_multipart_download())
        results.append(test_s3_range_download())  # NEW: Range GET
        test_s3_multipart_cleanup()
        
        # S3 TRANSFER ACCELERATION (NEW)
        #results.append(test_s3_accelerated_upload(test_data))
        #results.append(test_s3_accelerated_download())
        #test_s3_accelerated_cleanup()
        
        # SUMMARY
        print("\n" + "=" * 80)
        print("BENCHMARK SUMMARY")
        print("=" * 80)
        print(f"{'Step':<6} {'Operation':<50} {'Duration (s)':<15} {'Throughput (MB/s)':<20}")
        print("-" * 80)
        
        for result in results:
            print(f"{result['step']:<6} {result['operation']:<50} "
                  f"{result['duration_seconds']:<15.3f} {result['throughput_mbps']:<20.2f}")
        
        print("=" * 80)
        
        # Compare different approaches
        redis_upload = next(r for r in results if r['step'] == 1)
        redis_download = next(r for r in results if r['step'] == 2)
        s3_single_upload = next(r for r in results if r['step'] == 4)
        s3_single_download = next(r for r in results if r['step'] == 5)
        s3_managed_upload = next(r for r in results if r['step'] == 7)
        s3_managed_download = next(r for r in results if r['step'] == 8)
        s3_range_download = next(r for r in results if r['step'] == 10)
        s3_accel_upload = next(r for r in results if r['step'] == 12)
        s3_accel_download = next(r for r in results if r['step'] == 13)
        
        redis_total = redis_upload['duration_seconds'] + redis_download['duration_seconds']
        s3_single_total = s3_single_upload['duration_seconds'] + s3_single_download['duration_seconds']
        s3_managed_total = s3_managed_upload['duration_seconds'] + s3_managed_download['duration_seconds']
        s3_range_total = s3_managed_upload['duration_seconds'] + s3_range_download['duration_seconds']
        s3_accel_total = s3_accel_upload['duration_seconds'] + s3_accel_download['duration_seconds']
        
        print("\nCOMPARISON (Upload + Download):")
        print(f"Redis (SET + GET):                             {redis_total:.3f}s")
        print(f"S3 Single (PutObject + GetObject):             {s3_single_total:.3f}s")
        print(f"S3 Managed Multipart:                          {s3_managed_total:.3f}s")
        print(f"S3 Managed Upload + Range Download:            {s3_range_total:.3f}s")
        print(f"S3 Transfer Acceleration:                      {s3_accel_total:.3f}s")
        print()
        
        # Find winner
        comparisons = [
            ('Redis', redis_total),
            ('S3 Single', s3_single_total),
            ('S3 Managed', s3_managed_total),
            ('S3 Range', s3_range_total),
            ('S3 Acceleration', s3_accel_total)
        ]
        
        winner = min(comparisons, key=lambda x: x[1])
        
        print(f"🏆 Winner: {winner[0]} ({winner[1]:.3f}s total)")
        print()
        print("Speedup vs Redis:")
        for name, total in comparisons:
            if name != 'Redis':
                speedup = redis_total / total
                diff = redis_total - total
                print(f"  {name:<25} {speedup:.2f}x faster ({diff:+.3f}s)")
        
        return {
            'statusCode': 200,
            'body': json.dumps({
                'message': 'Benchmark completed successfully',
                'results': results,
                'summary': {
                    'redis_total_seconds': redis_total,
                    's3_single_total_seconds': s3_single_total,
                    's3_managed_total_seconds': s3_managed_total,
                    's3_range_total_seconds': s3_range_total,
                    's3_acceleration_total_seconds': s3_accel_total,
                    'winner': winner[0]
                }
            })
        }
        
    except Exception as e:
        print(f"\n❌ ERROR: {str(e)}")
        import traceback
        traceback.print_exc()
        
        return {
            'statusCode': 500,
            'body': json.dumps({
                'error': str(e),
                'results': results
            })
        }