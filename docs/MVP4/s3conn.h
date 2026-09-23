#pragma once

#include <aws/s3/S3Client.h>
#include <aws/core/auth/AWSCredentialsProvider.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum S3Status : unsigned int {
    S3_OK = 0,
    S3_ERR_EMPTY_DATA = 1,
    S3_ERR_PUT_FAILED = 2,
    S3_ERR_GET_FAILED = 3,
    S3_ERR_GET_EMPTY_BODY = 4,
    S3_ERR_NO_CREDENTIALS = 5,
    S3_ERR_ASSUME_ROLE_FAILED = 6,
    S3_ERR_MISSING_ROLE_ARN = 7,
    S3_ERR_DELETE_FAILED = 8,
    S3_ERR_LIST_FAILED = 9,
    S3_ERR_NOT_FOUND = 10,
};

class S3Connector {
public:
    explicit S3Connector(const std::string& region = "eu-west-1");
    ~S3Connector() = default;

    S3Connector(const S3Connector&) = delete;
    S3Connector& operator=(const S3Connector&) = delete;

    S3Connector(S3Connector&&) = delete;
    S3Connector& operator=(S3Connector&&) = delete;

    bool IsValid() const { return status_ == S3_OK; }
    S3Status GetStatus() const { return status_; }
    const std::string& GetStatusMessage() const { return status_message_; }

    S3Status PutString(
        const std::string& bucket,
        const std::string& key,
        const std::string& data,
        std::string* error = nullptr);

    S3Status GetObjectToString(
        const std::string& bucket,
        const std::string& key,
        std::string& output,
        std::string* error = nullptr);

    S3Status GetObjectRange(
        const std::string& bucket,
        const std::string& key,
        int64_t offset,
        int64_t length,
        std::string& output,
        std::string* error = nullptr);

    S3Status GetObjectSize(
        const std::string& bucket,
        const std::string& key,
        int64_t& size,
        std::string* error = nullptr);

    S3Status DeleteObject(
        const std::string& bucket,
        const std::string& key,
        std::string* error = nullptr);

    std::vector<std::string> ListObjectKeys(
        const std::string& bucket,
        const std::string& prefix,
        std::string* error = nullptr);

private:
    Aws::S3::S3Client client_;

    S3Status status_ = S3_OK;
    std::string status_message_;
    std::string region_;

    static void SetError(
        std::string* error,
        const std::string& msg);
};
