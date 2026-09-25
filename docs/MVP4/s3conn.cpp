```cpp
#include "xena/s3_connector/S3Connector.h"
#include "mto/mtoapi/MtoLogger.h"

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>

#include <aws/s3/S3Client.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/PutObjectRequest.h>

#include <aws/sts/STSClient.h>
#include <aws/sts/model/AssumeRoleRequest.h>

/*
 * AWS SDK 1.11.x:
 *
 * STSAssumeRoleCredentialsProvider is provided by the identity-management
 * component of the SDK.
 */
#include <aws/identity-management/auth/STSAssumeRoleCredentialsProvider.h>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <sstream>


namespace
{

// ============================================================================
// AWS SDK initialization
// ============================================================================
//
// AWS SDK initialization is process-level.
//
// We intentionally initialize the SDK once and do NOT call ShutdownAPI() from
// S3Connector. XENA/GRID shared libraries can be loaded/unloaded independently
// and the AWS SDK documentation warns about static destruction ordering.
//
// The SDK therefore remains initialized for the lifetime of the process.
// ============================================================================

void EnsureAwsSdkInitialized()
{
    static std::once_flag initialized;

    std::call_once(initialized, []()
    {
        /*
         * Keep the SDK options alive for the process lifetime.
         *
         * We intentionally do not call Aws::ShutdownAPI().
         */
        static Aws::SDKOptions options;

        Aws::InitAPI(options);

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::info,
            "S3Connector: AWS SDK initialized "
            "(process-level, single owner)");
    });
}


// ============================================================================
// Common client configuration
// ============================================================================

template <typename Config>
void ApplyCommonConfig(
    Config& config,
    const std::string& region)
{
    config.region = region;

    config.verifySSL = true;

    /*
     * One S3Client is shared by all adapters in the processing POD.
     *
     * Current workload:
     *   - 5 deals per adapter
     *   - relatively low S3 concurrency
     *
     * Start with 10 connections and tune using measured concurrency.
     *
     * This is a maximum pool size; it does NOT mean 10 TCP connections are
     * opened immediately.
     */
    config.maxConnections = 10;

    config.connectTimeoutMs = 5000;
    config.requestTimeoutMs = 30000;
    config.httpRequestTimeoutMs = 60000;

    /*
     * Preserve the existing CA-bundle behavior.
     */
    const char* caBundle = std::getenv("AWS_CA_BUNDLE");

    if (caBundle && *caBundle)
    {
        config.caFile = Aws::String(caBundle);
    }

    /*
     * Preserve proxy configuration from the environment.
     *
     * Expected format:
     *
     *     http://proxy.example.com:8080
     *     https://proxy.example.com:8080
     *
     * If your current deployment obtains proxyHost/proxyPort through some
     * other configuration mechanism, keep that logic instead.
     */
    const char* proxyEnv = std::getenv("HTTPS_PROXY");

    if (!proxyEnv || !*proxyEnv)
    {
        proxyEnv = std::getenv("https_proxy");
    }

    if (proxyEnv && *proxyEnv)
    {
        std::string proxy(proxyEnv);

        Aws::Http::Scheme proxyScheme =
            Aws::Http::Scheme::HTTPS;

        const std::string httpsPrefix = "https://";
        const std::string httpPrefix = "http://";

        if (proxy.rfind(httpsPrefix, 0) == 0)
        {
            proxy.erase(0, httpsPrefix.size());
            proxyScheme = Aws::Http::Scheme::HTTPS;
        }
        else if (proxy.rfind(httpPrefix, 0) == 0)
        {
            proxy.erase(0, httpPrefix.size());
            proxyScheme = Aws::Http::Scheme::HTTP;
        }

        const auto separator = proxy.rfind(':');

        if (separator != std::string::npos)
        {
            const std::string host =
                proxy.substr(0, separator);

            const std::string port =
                proxy.substr(separator + 1);

            try
            {
                config.proxyHost = host.c_str();

                config.proxyPort =
                    static_cast<unsigned>(
                        std::stoul(port));

                config.proxyScheme = proxyScheme;
            }
            catch (...)
            {
                mtoapi::MtoLogger::log(
                    mtoapi::LogLevel::warn,
                    "S3Connector: Invalid proxy configuration: " +
                        proxy);
            }
        }
        else
        {
            config.proxyHost = proxy.c_str();
            config.proxyScheme = proxyScheme;
        }
    }
}

} // namespace


// ============================================================================
// Constructor
// ============================================================================

S3Connector::S3Connector(const std::string& region)
    : region_(region)
{
    EnsureAwsSdkInitialized();

    const bool isTestEnv =
        std::getenv("XENA_TEST") != nullptr;


    // =========================================================================
    // TEST ENVIRONMENT
    // =========================================================================

    if (isTestEnv)
    {
        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::info,
            "S3Connector: XENA_TEST detected - "
            "using local credentials, skipping STS AssumeRole");


        auto credProvider =
            Aws::MakeShared<
                Aws::Auth::DefaultAWSCredentialsProviderChain>(
                    "S3Connector");

        const auto baseCreds =
            credProvider->GetAWSCredentials();

        if (baseCreds.IsEmpty())
        {
            const std::string msg =
                "[TEST MODE] No local AWS credentials found. "
                "Set AWS_ACCESS_KEY_ID + AWS_SECRET_ACCESS_KEY "
                "in your environment.";

            mtoapi::MtoLogger::log(
                mtoapi::LogLevel::error,
                "S3Connector: " + msg);

            status_ = S3_ERR_NO_CREDENTIALS;
            status_message_ = msg;

            return;
        }


        Aws::S3::S3ClientConfiguration s3Config;

        ApplyCommonConfig(
            s3Config,
            region_);


        /*
         * In test mode we do not need STS.
         *
         * Keep the credentials in a provider so the S3Client has the same
         * ownership model as production.
         */
        auto s3CredentialsProvider =
            Aws::MakeShared<
                Aws::Auth::SimpleAWSCredentialsProvider>(
                    "S3Connector",
                    baseCreds.GetAWSAccessKeyId(),
                    baseCreds.GetAWSSecretKey(),
                    baseCreds.GetSessionToken());


        client_.emplace(
            s3CredentialsProvider,
            nullptr,
            s3Config);


        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::info,
            "S3Connector: [TEST MODE] S3 client created OK "
            "for region: " + region_);

        return;
    }


    // =========================================================================
    // PRODUCTION / DEV
    // =========================================================================

    const char* roleArnEnv =
        std::getenv("XENA_AWS_ROLE_ARN");

    if (!roleArnEnv || !*roleArnEnv)
    {
        const std::string msg =
            "XENA_AWS_ROLE_ARN environment variable is not set";

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + msg);

        status_ = S3_ERR_MISSING_ROLE_ARN;
        status_message_ = msg;

        return;
    }

    const std::string roleArn(roleArnEnv);

    mtoapi::MtoLogger::log(
        mtoapi::LogLevel::info,
        "S3Connector: Using role ARN: " + roleArn);


    // =========================================================================
    // BASE CREDENTIALS
    // =========================================================================
    //
    // These credentials are used by STS to assume the target role.
    //
    // We still validate them explicitly because the existing S3Connector API
    // reports S3_ERR_NO_CREDENTIALS during construction.
    // =========================================================================

    auto baseCredProvider =
        Aws::MakeShared<
            Aws::Auth::DefaultAWSCredentialsProviderChain>(
                "S3Connector");

    const auto baseCreds =
        baseCredProvider->GetAWSCredentials();

    if (baseCreds.IsEmpty())
    {
        const std::string msg =
            "No base AWS credentials found. "
            "Set AWS_ACCESS_KEY_ID + AWS_SECRET_ACCESS_KEY, "
            "configure ~/.aws/credentials, or attach an IAM role.";

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + msg);

        status_ = S3_ERR_NO_CREDENTIALS;
        status_message_ = msg;

        return;
    }


    // =========================================================================
    // STS CLIENT
    // =========================================================================

    Aws::STS::STSClientConfiguration stsConfig;

    ApplyCommonConfig(
        stsConfig,
        region_);


    /*
     * The STS client is created once for this S3Connector.
     *
     * It is then owned by the credentials provider.
     *
     * We do NOT call AssumeRole() manually anymore.
     */
    auto stsClient =
        Aws::MakeShared<Aws::STS::STSClient>(
            "S3Connector",
            baseCreds,
            nullptr,
            stsConfig);


    // =========================================================================
        // ASSUME ROLE
        // =========================================================================
        
        Aws::STS::Model::AssumeRoleRequest assumeRoleRequest;
        
        assumeRoleRequest.SetRoleArn(
            Aws::String(roleArn.c_str()));
        
        assumeRoleRequest.SetRoleSessionName(
            "xena-s3-session");
        
        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::info,
            "S3Connector: Assuming role: " + roleArn);
        
        const auto assumeRoleOutcome =
            stsClient->AssumeRole(assumeRoleRequest);
        
        if (!assumeRoleOutcome.IsSuccess())
        {
            const auto& err =
                assumeRoleOutcome.GetError();
        
            const std::string msg =
                "STS AssumeRole failed [" +
                std::string(err.GetExceptionName().c_str()) +
                "]: " +
                std::string(err.GetMessage().c_str());
        
            mtoapi::MtoLogger::log(
                mtoapi::LogLevel::error,
                "S3Connector: " + msg);
        
            status_ = S3_ERR_ASSUME_ROLE_FAILED;
            status_message_ = msg;
        
            return;
        }
        
        const auto& credentials =
            assumeRoleOutcome.GetResult().GetCredentials();
        
        const Aws::Auth::AWSCredentials assumedCredentials(
            credentials.GetAccessKeyId(),
            credentials.GetSecretAccessKey(),
            credentials.GetSessionToken());
        
        
        // =========================================================================
        // S3 CREDENTIAL PROVIDER
        // =========================================================================
        
        auto s3CredentialsProvider =
            Aws::MakeShared<
                Aws::Auth::SimpleAWSCredentialsProvider>(
                    "S3Connector",
                    assumedCredentials);
        
        
        // =========================================================================
        // S3 CLIENT
        // =========================================================================
        
        Aws::S3::S3ClientConfiguration s3Config;
        
        ApplyCommonConfig(
            s3Config,
            region_);
        
        client_.emplace(
            s3CredentialsProvider,
            nullptr,
            s3Config);
        
        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::info,
            "S3Connector: Successfully assumed role and "
            "created S3 client for region: " + region_);
}


// ============================================================================
// PutString
// ============================================================================

S3Status S3Connector::PutString(
    const std::string& bucket,
    const std::string& key,
    const std::string& data,
    std::string* error)
{
    if (!IsValid() || !client_)
    {
        const std::string msg =
            "S3Connector not initialized: " +
            status_message_;

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + msg);

        SetError(error, msg);

        return status_;
    }


    if (data.empty())
    {
        const std::string msg =
            "Cannot upload empty data";

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + msg);

        SetError(error, msg);

        return S3_ERR_EMPTY_DATA;
    }


    mtoapi::MtoLogger::log(
        mtoapi::LogLevel::info,
        "S3Connector: Uploading to s3://" +
            bucket + "/" + key +
            " (" + std::to_string(data.size()) +
            " bytes)");


    Aws::S3::Model::PutObjectRequest req;

    req.SetBucket(bucket.c_str());
    req.SetKey(key.c_str());
    req.SetContentLength(
        static_cast<long long>(data.size()));
    req.SetContentType(
        "application/octet-stream");


    auto body =
        Aws::MakeShared<Aws::StringStream>(
            "S3ConnectorPutString");

    body->write(
        data.data(),
        static_cast<std::streamsize>(
            data.size()));

    body->flush();

    req.SetBody(body);


    const auto t0 =
        std::chrono::steady_clock::now();

    auto outcome =
        client_->PutObject(req);

    const auto ms =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
            .count();


    mtoapi::MtoLogger::log(
        mtoapi::LogLevel::info,
        "S3Connector: PutObject to s3://" +
            bucket + "/" + key +
            " took " +
            std::to_string(ms) +
            " ms");


    if (!outcome.IsSuccess())
    {
        const auto& err =
            outcome.GetError();

        const std::string errMsg =
            "S3 PUT failed [" +
            std::string(
                err.GetExceptionName().c_str()) +
            "]: " +
            std::string(
                err.GetMessage().c_str());

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + errMsg);

        SetError(error, errMsg);

        return S3_ERR_PUT_FAILED;
    }


    mtoapi::MtoLogger::log(
        mtoapi::LogLevel::info,
        "S3Connector: Successfully uploaded to s3://" +
            bucket + "/" + key);

    return S3_OK;
}


// ============================================================================
// GetObjectToString
// ============================================================================

S3Status S3Connector::GetObjectToString(
    const std::string& bucket,
    const std::string& key,
    std::string& output,
    std::string* error)
{
    if (!IsValid() || !client_)
    {
        const std::string msg =
            "S3Connector not initialized: " +
            status_message_;

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + msg);

        SetError(error, msg);

        return status_;
    }


    mtoapi::MtoLogger::log(
        mtoapi::LogLevel::info,
        "S3Connector: Downloading from s3://" +
            bucket + "/" + key);


    Aws::S3::Model::GetObjectRequest req;

    req.SetBucket(bucket.c_str());
    req.SetKey(key.c_str());


    auto outcome =
        client_->GetObject(req);


    if (!outcome.IsSuccess())
    {
        const auto& err =
            outcome.GetError();

        const std::string errMsg =
            "S3 GET failed [" +
            std::string(
                err.GetExceptionName().c_str()) +
            "]: " +
            std::string(
                err.GetMessage().c_str());

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + errMsg);

        SetError(error, errMsg);

        return S3_ERR_GET_FAILED;
    }


    /*
     * Store the result before accessing the body.
     */
    auto result =
        outcome.GetResultWithOwnership();

    auto& body =
        result.GetBody();


    std::ostringstream ss;

    ss << body.rdbuf();

    output = ss.str();


    if (output.empty())
    {
        const std::string errMsg =
            "S3 GET returned empty body for key: " +
            key;

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + errMsg);

        SetError(error, errMsg);

        return S3_ERR_GET_EMPTY_BODY;
    }


    mtoapi::MtoLogger::log(
        mtoapi::LogLevel::info,
        "S3Connector: Successfully downloaded from s3://" +
            bucket + "/" + key +
            " (" +
            std::to_string(output.size()) +
            " bytes)");

    return S3_OK;
}


// ============================================================================
// GetObjectRange
// ============================================================================

S3Status S3Connector::GetObjectRange(
    const std::string& bucket,
    const std::string& key,
    int64_t offset,
    int64_t length,
    std::string& output,
    std::string* error)
{
    if (!IsValid() || !client_)
    {
        const std::string msg =
            "S3Connector not initialized: " +
            status_message_;

        SetError(error, msg);

        return status_;
    }


    if (offset < 0 || length <= 0)
    {
        const std::string msg =
            "Invalid S3 range: offset=" +
            std::to_string(offset) +
            ", length=" +
            std::to_string(length);

        SetError(error, msg);

        return S3_ERR_GET_FAILED;
    }


    const int64_t end =
        offset + length - 1;


    Aws::S3::Model::GetObjectRequest req;

    req.SetBucket(bucket.c_str());
    req.SetKey(key.c_str());

    req.SetRange(
        "bytes=" +
        std::to_string(offset) +
        "-" +
        std::to_string(end));


    auto outcome =
        client_->GetObject(req);


    if (!outcome.IsSuccess())
    {
        const auto& err =
            outcome.GetError();

        const std::string errMsg =
            "S3 GET range failed [" +
            std::string(
                err.GetExceptionName().c_str()) +
            "]: " +
            std::string(
                err.GetMessage().c_str());

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + errMsg);

        SetError(error, errMsg);

        return S3_ERR_GET_FAILED;
    }


    auto result =
        outcome.GetResultWithOwnership();

    auto& body =
        result.GetBody();


    std::ostringstream ss;

    ss << body.rdbuf();

    output = ss.str();


    if (output.empty())
    {
        const std::string errMsg =
            "S3 GET range returned empty body for key: " +
            key;

        SetError(error, errMsg);

        return S3_ERR_GET_EMPTY_BODY;
    }


    return S3_OK;
}


// ============================================================================
// GetObjectSize
// ============================================================================

S3Status S3Connector::GetObjectSize(
    const std::string& bucket,
    const std::string& key,
    int64_t& size,
    std::string* error)
{
    if (!IsValid() || !client_)
    {
        const std::string msg =
            "S3Connector not initialized: " +
            status_message_;

        SetError(error, msg);

        return status_;
    }


    Aws::S3::Model::HeadObjectRequest req;

    req.SetBucket(bucket.c_str());
    req.SetKey(key.c_str());


    auto outcome =
        client_->HeadObject(req);


    if (!outcome.IsSuccess())
    {
        const auto& err =
            outcome.GetError();

        const std::string errMsg =
            "S3 HEAD failed [" +
            std::string(
                err.GetExceptionName().c_str()) +
            "]: " +
            std::string(
                err.GetMessage().c_str());

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + errMsg);

        SetError(error, errMsg);

        if (err.GetResponseCode() ==
            Aws::Http::HttpResponseCode::NOT_FOUND)
        {
            return S3_ERR_NOT_FOUND;
        }

        return S3_ERR_GET_FAILED;
    }


    size =
        outcome.GetResult().GetContentLength();

    return S3_OK;
}


// ============================================================================
// DeleteObject
// ============================================================================

S3Status S3Connector::DeleteObject(
    const std::string& bucket,
    const std::string& key,
    std::string* error)
{
    if (!IsValid() || !client_)
    {
        const std::string msg =
            "S3Connector not initialized: " +
            status_message_;

        SetError(error, msg);

        return status_;
    }


    Aws::S3::Model::DeleteObjectRequest req;

    req.SetBucket(bucket.c_str());
    req.SetKey(key.c_str());


    auto outcome =
        client_->DeleteObject(req);


    if (!outcome.IsSuccess())
    {
        const auto& err =
            outcome.GetError();

        const std::string errMsg =
            "S3 DELETE failed [" +
            std::string(
                err.GetExceptionName().c_str()) +
            "]: " +
            std::string(
                err.GetMessage().c_str());

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            "S3Connector: " + errMsg);

        SetError(error, errMsg);

        return S3_ERR_DELETE_FAILED;
    }


    return S3_OK;
}


// ============================================================================
// ListObjectKeys
// ============================================================================

std::vector<std::string> S3Connector::ListObjectKeys(
    const std::string& bucket,
    const std::string& prefix,
    std::string* error)
{
    std::vector<std::string> keys;


    if (!IsValid() || !client_)
    {
        const std::string msg =
            "S3Connector not initialized: " +
            status_message_;

        SetError(error, msg);

        return keys;
    }


    Aws::String continuationToken;


    do
    {
        Aws::S3::Model::ListObjectsV2Request req;

        req.SetBucket(bucket.c_str());
        req.SetPrefix(prefix.c_str());


        if (!continuationToken.empty())
        {
            req.SetContinuationToken(
                continuationToken);
        }


        auto outcome =
            client_->ListObjectsV2(req);


        if (!outcome.IsSuccess())
        {
            const auto& err =
                outcome.GetError();

            const std::string errMsg =
                "S3 LIST failed [" +
                std::string(
                    err.GetExceptionName().c_str()) +
                "]: " +
                std::string(
                    err.GetMessage().c_str());

            mtoapi::MtoLogger::log(
                mtoapi::LogLevel::error,
                "S3Connector: " + errMsg);

            SetError(error, errMsg);

            return {};
        }


        const auto& contents =
            outcome.GetResult().GetContents();


        for (const auto& object : contents)
        {
            keys.emplace_back(
                object.GetKey().c_str());
        }


        const auto& result =
            outcome.GetResult();


        if (result.GetIsTruncated())
        {
            continuationToken =
                result.GetNextContinuationToken();
        }
        else
        {
            continuationToken.clear();
        }

    } while (!continuationToken.empty());


    return keys;
}


// ============================================================================
// Error helper
// ============================================================================

void S3Connector::SetError(
    std::string* error,
    const std::string& msg)
{
    if (error)
    {
        *error = msg;
    }
}
```
