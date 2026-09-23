#include <arrow/api.h>
#include <arrow/io/memory.h>
#include <arrow/ipc/writer.h>   // Required for arrow::ipc::MakeStreamWriter
#include <arrow/util/base64.h>   // Required for arrow::util::base64_encode

std::string XDEAdapter::serialize_fmtm_payload(
    const std::string& input_file,
    const std::string& results_path,
    const std::string& error_path,
    const std::string& calibrated_market,
    const std::string& session_id,
    std::size_t row_group,
    std::size_t row_index,
    std::size_t deal_count,
    const std::string& progress_counter_key,
    const std::string& progress_book_id,
    const std::string& task_id,
    bool reconciliation,
    const std::string& embedded_row_group_data,
    const std::optional<int>& retries) 
{
    std::ostringstream os;
    os << "{";
    os << "\"job_type\":\"fmtm_job\",";
    os << "\"input_file\":\"" << escape_json(input_file) << "\",";
    os << "\"results_path\":\"" << escape_json(results_path) << "\",";
    os << "\"error_path\":\"" << escape_json(error_path) << "\",";
    os << "\"calibrated_market\":\"" << escape_json(calibrated_market) << "\",";
    os << "\"session_id\":\"" << escape_json(session_id) << "\",";
    os << "\"row_group\":" << row_group << ",";
    os << "\"row_index\":" << row_index << ",";
    os << "\"row_group_data\":\"" << escape_json(embedded_row_group_data) << "\",";
    os << "\"deal_count\":" << deal_count << ",";
    os << "\"progress_counter_key\":\"" << escape_json(progress_counter_key) << "\",";
    os << "\"progress_book_id\":\"" << escape_json(progress_book_id) << "\",";
    os << "\"task_id\":\"" << escape_json(task_id) << "\",";
    os << "\"reconciliation\":" << (reconciliation ? "true" : "false");
    if (retries.has_value()) {
        os << ",\"retries\":" << *retries;
    }
    os << "}";
    return os.str();
}


// --- Phase 1: Collect one FMTM payload per deal slice ---
std::vector<std::string> all_payloads;
std::vector<int> job_deal_counts;

all_payloads.reserve(static_cast<std::size_t>(
    (deal_count + kDealsPerJob - 1) / kDealsPerJob));
job_deal_counts.reserve(all_payloads.capacity());

for (int row_group = 0; row_group < row_group_count; ++row_group) {
    const int64_t deals_in_row_group =
        row_group_deal_counts[static_cast<std::size_t>(row_group)];

    mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
        "XDEAdapter: Scheduling row group " + std::to_string(row_group + 1) +
        "/" + std::to_string(row_group_count) + " (" +
        std::to_string(deals_in_row_group) + " deals, " +
        std::to_string((deals_in_row_group +
            static_cast<int64_t>(kDealsPerJob) - 1) /
            static_cast<int64_t>(kDealsPerJob)) +
        " job(s))");

    // Re-use parquet_reader instance to read the row group into memory
    std::shared_ptr<arrow::Table> full_rg_table;
    auto read_status = parquet_reader->ReadRowGroup(row_group, &full_rg_table);
    if (!read_status.ok()) {
        const std::string msg =
            "XDEAdapter: Failed to read row group " +
            std::to_string(row_group) + ": " +
            read_status.ToString();

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    // Validate that Arrow returned the same number of rows as Parquet metadata
    if (!full_rg_table) {
        const std::string msg =
            "XDEAdapter: ReadRowGroup returned null table for row group " +
            std::to_string(row_group);

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    if (full_rg_table->num_rows() != deals_in_row_group) {
        const std::string msg =
            "XDEAdapter: Row group row count mismatch. row_group=" +
            std::to_string(row_group) +
            ", metadata_rows=" +
            std::to_string(deals_in_row_group) +
            ", table_rows=" +
            std::to_string(full_rg_table->num_rows());

        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    // Sub-chunk iteration with zero-copy table slicing
    for (int64_t deal_index = 0;
         deal_index < deals_in_row_group;
         deal_index += static_cast<int64_t>(kDealsPerJob)) {

        const int deal_count_for_job = static_cast<int>(std::min<int64_t>(
            static_cast<int64_t>(kDealsPerJob),
            deals_in_row_group - deal_index));

        // 1. Slice the Arrow Table in memory for EXACTLY this sub-chunk (Zero-Copy)
        std::shared_ptr<arrow::Table> sliced_table =
            full_rg_table->Slice(deal_index, deal_count_for_job);

        // 2. Serialize ONLY the sliced table slice to Arrow IPC Stream
        auto output_stream_result =
            arrow::io::BufferOutputStream::Create();

        if (!output_stream_result.ok()) {
            const std::string msg =
                "XDEAdapter: Failed to create Arrow output stream for row group " +
                std::to_string(row_group) +
                ", deal index " + std::to_string(deal_index) +
                ": " + output_stream_result.status().ToString();

            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        auto output_stream = *output_stream_result;

        auto writer_result =
            arrow::ipc::MakeStreamWriter(
                output_stream,
                sliced_table->schema());

        if (!writer_result.ok()) {
            const std::string msg =
                "XDEAdapter: Failed to create Arrow IPC writer for row group " +
                std::to_string(row_group) +
                ", deal index " + std::to_string(deal_index) +
                ": " + writer_result.status().ToString();

            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        auto writer = *writer_result;

        auto write_status = writer->WriteTable(*sliced_table);
        if (!write_status.ok()) {
            const std::string msg =
                "XDEAdapter: Failed to serialize Arrow table for row group " +
                std::to_string(row_group) +
                ", deal index " + std::to_string(deal_index) +
                ": " + write_status.ToString();

            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        auto close_status = writer->Close();
        if (!close_status.ok()) {
            const std::string msg =
                "XDEAdapter: Failed to close Arrow IPC writer for row group " +
                std::to_string(row_group) +
                ", deal index " + std::to_string(deal_index) +
                ": " + close_status.ToString();

            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        auto buffer_result = output_stream->Finish();
        if (!buffer_result.ok()) {
            const std::string msg =
                "XDEAdapter: Failed to finish Arrow output stream for row group " +
                std::to_string(row_group) +
                ", deal index " + std::to_string(deal_index) +
                ": " + buffer_result.status().ToString();

            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        auto buffer = *buffer_result;

        std::string raw_bytes(
            reinterpret_cast<const char*>(buffer->data()),
            buffer->size());

        // 3. Base64 encode the binary slice
        std::string embedded_slice_data = arrow::util::base64_encode(raw_bytes);

        all_payloads.push_back(serialize_fmtm_payload(
            inputPath,
            fmtm_out_path,
            errorPath,
            calibratedMarket,
            sessionId,
            static_cast<std::size_t>(row_group),
            static_cast<std::size_t>(deal_index),
            static_cast<std::size_t>(deal_count_for_job),
            progress_redis_ready ? progressCounterKey : "",
            progress_redis_ready ? bookId : "",
            taskId,
            reconciliation,
            embedded_slice_data,
            requestRetries));

        job_deal_counts.push_back(deal_count_for_job);
    }
}



-----------


#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Adjust these member names to the actual Deal class if necessary.

inline void to_json(nlohmann::json& j, const Deal& deal)
{
    j = nlohmann::json{
        {"dealId", deal.name},
        {"trade", deal.trade},
        {"collateral", deal.collateral},
        {"currency", deal.currency},
        {"nettingSet", deal.nettingSet},
        {"csa", deal.csa},
        {"legalStructureContext", deal.legalStructureContext}
    };
}

inline void from_json(const nlohmann::json& j, Deal& deal)
{
    deal.name =
        j.at("dealId").get<std::string>();

    deal.trade =
        j.at("trade").get<std::string>();

    deal.collateral =
        j.at("collateral").get<std::string>();

    deal.currency =
        j.at("currency").get<std::string>();

    deal.nettingSet =
        j.at("nettingSet").get<std::string>();

    deal.csa =
        j.at("csa").get<std::string>();

    deal.legalStructureContext =
        j.at("legalStructureContext").get<std::string>();
}


-------


#include "xde_adapter.h"
#include "deal_json.h"

#include <arrow/api.h>
#include <arrow/table.h>
#include <parquet/arrow/reader.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace
{
constexpr std::size_t kMaxSqsMessageSize = 256 * 1024;
}


// -----------------------------------------------------------------------------
// Serialize FMTM payload
// -----------------------------------------------------------------------------

std::string XDEAdapter::serialize_fmtm_payload(
    const std::string& input_file,
    const std::string& results_path,
    const std::string& error_path,
    const std::string& calibrated_market,
    const std::string& session_id,
    std::size_t row_group,
    std::size_t deal_count,
    const std::string& progress_counter_key,
    const std::string& progress_book_id,
    const std::string& task_id,
    bool reconciliation,
    const std::vector<Deal>& deals,
    const std::optional<int>& retries)
{
    nlohmann::json payload = {
        {"job_type", "fmtm_job"},
        {"input_file", input_file},
        {"results_path", results_path},
        {"error_path", error_path},
        {"calibrated_market", calibrated_market},
        {"session_id", session_id},
        {"row_group", row_group},
        {"deal_count", deal_count},
        {"progress_counter_key", progress_counter_key},
        {"progress_book_id", progress_book_id},
        {"task_id", task_id},
        {"reconciliation", reconciliation},
        {"deals", deals}
    };

    if (retries.has_value())
    {
        payload["retries"] = *retries;
    }

    return payload.dump();
}


// -----------------------------------------------------------------------------
// Build FMTM payloads from the Parquet reader
// -----------------------------------------------------------------------------

std::pair<int, std::string> XDEAdapter::build_fmtm_payloads(
    const std::shared_ptr<parquet::arrow::FileReader>& parquet_reader,
    int num_row_groups,
    const std::vector<int64_t>& row_group_deal_counts,
    int64_t deal_count,
    const std::string& inputPath,
    const std::string& fmtm_out_path,
    const std::string& errorPath,
    const std::string& calibratedMarket,
    const std::string& sessionId,
    const std::string& progressCounterKey,
    const std::string& bookId,
    const std::string& taskId,
    bool reconciliation,
    const std::optional<int>& requestRetries,
    std::vector<std::string>& all_payloads,
    std::vector<int>& job_deal_counts)
{
    if (!parquet_reader)
    {
        const std::string msg =
            "XDEAdapter: Parquet reader is null";

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            msg);

        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    if (num_row_groups < 0)
    {
        const std::string msg =
            "XDEAdapter: Invalid number of row groups: " +
            std::to_string(num_row_groups);

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            msg);

        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    if (static_cast<std::size_t>(num_row_groups) !=
        row_group_deal_counts.size())
    {
        const std::string msg =
            "XDEAdapter: Row group metadata mismatch: "
            "num_row_groups=" +
            std::to_string(num_row_groups) +
            ", row_group_deal_counts=" +
            std::to_string(row_group_deal_counts.size());

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            msg);

        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    //
    // Process each Parquet RowGroup exactly once.
    //
    for (int row_group = 0;
         row_group < num_row_groups;
         ++row_group)
    {
        std::shared_ptr<arrow::Table> full_rg_table;

        //
        // Read the complete RowGroup once.
        //
        const arrow::Status read_status =
            parquet_reader->ReadRowGroup(
                row_group,
                &full_rg_table);

        if (!read_status.ok() || !full_rg_table)
        {
            const std::string msg =
                "XDEAdapter: Failed to read row group " +
                std::to_string(row_group) +
                ": " +
                (read_status.ok()
                     ? "ReadRowGroup returned a null table"
                     : read_status.ToString());

            mtoapi::MtoLogger::log(
                mtoapi::LogLevel::error,
                msg);

            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        const int64_t deals_in_row_group =
            full_rg_table->num_rows();

        //
        // Validate the actual RowGroup size against the Parquet
        // metadata collected before processing.
        //
        if (deals_in_row_group !=
            row_group_deal_counts[row_group])
        {
            const std::string msg =
                "XDEAdapter: Row group " +
                std::to_string(row_group) +
                " row count mismatch: metadata=" +
                std::to_string(
                    row_group_deal_counts[row_group]) +
                ", actual=" +
                std::to_string(deals_in_row_group);

            mtoapi::MtoLogger::log(
                mtoapi::LogLevel::error,
                msg);

            pushErrorLog(msg);

            return {
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                make_result_json(
                    "error",
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    msg)
            };
        }

        //
        // Split the RowGroup into logical FMTM jobs.
        //
        for (int64_t deal_index = 0;
             deal_index < deals_in_row_group;
             deal_index +=
                 static_cast<int64_t>(kDealsPerJob))
        {
            const int64_t deal_count_for_job =
                std::min<int64_t>(
                    static_cast<int64_t>(kDealsPerJob),
                    deals_in_row_group - deal_index);

            //
            // Zero-copy slice of the RowGroup.
            //
            const std::shared_ptr<arrow::Table> sliced_table =
                full_rg_table->Slice(
                    deal_index,
                    deal_count_for_job);

            try
            {
                // ---------------------------------------------------------
                // Convert the sliced Table into RecordBatch(es).
                //
                // kDealsPerJob defines the logical FMTM job size, so we
                // expect exactly one RecordBatch containing the slice.
                // ---------------------------------------------------------

                arrow::TableBatchReader batch_reader(*sliced_table);

                batch_reader.set_chunksize(
                    deal_count_for_job);

                std::shared_ptr<arrow::RecordBatch> record_batch;

                arrow::Status status =
                    batch_reader.ReadNext(&record_batch);

                if (!status.ok())
                {
                    const std::string msg =
                        "XDEAdapter: Failed to create RecordBatch "
                        "for row group " +
                        std::to_string(row_group) +
                        ", deal index " +
                        std::to_string(deal_index) +
                        ": " +
                        status.ToString();

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                if (!record_batch)
                {
                    const std::string msg =
                        "XDEAdapter: Empty RecordBatch for row group " +
                        std::to_string(row_group) +
                        ", deal index " +
                        std::to_string(deal_index);

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                //
                // The RecordBatch must contain exactly the requested
                // number of deals.
                //
                if (record_batch->num_rows() !=
                    deal_count_for_job)
                {
                    const std::string msg =
                        "XDEAdapter: RecordBatch row count mismatch "
                        "for row group " +
                        std::to_string(row_group) +
                        ", deal index " +
                        std::to_string(deal_index) +
                        ": expected " +
                        std::to_string(deal_count_for_job) +
                        ", got " +
                        std::to_string(record_batch->num_rows());

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                //
                // Make sure TableBatchReader did not produce another
                // RecordBatch. One FMTM job must correspond to exactly
                // one RecordBatch.
                //
                std::shared_ptr<arrow::RecordBatch> extra_batch;

                status =
                    batch_reader.ReadNext(&extra_batch);

                if (!status.ok())
                {
                    const std::string msg =
                        "XDEAdapter: Failed while checking for "
                        "additional RecordBatches for row group " +
                        std::to_string(row_group) +
                        ", deal index " +
                        std::to_string(deal_index) +
                        ": " +
                        status.ToString();

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                if (extra_batch)
                {
                    const std::string msg =
                        "XDEAdapter: Unexpected multiple RecordBatches "
                        "for row group " +
                        std::to_string(row_group) +
                        ", deal index " +
                        std::to_string(deal_index);

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                // ---------------------------------------------------------
                // Arrow RecordBatch -> business-level Deal objects.
                // ---------------------------------------------------------

                std::vector<Deal> chunk_deals =
                    deal_decompositor_.DecomposeRecordBatch(
                        record_batch);

                //
                // Strong invariant:
                //
                // Parquet rows
                //      ==
                // Arrow slice rows
                //      ==
                // RecordBatch rows
                //      ==
                // Deal objects
                //
                if (static_cast<int64_t>(chunk_deals.size()) !=
                    deal_count_for_job)
                {
                    const std::string msg =
                        "XDEAdapter: Deal decomposition count mismatch "
                        "in row group " +
                        std::to_string(row_group) +
                        ", deal index " +
                        std::to_string(deal_index) +
                        ": expected " +
                        std::to_string(deal_count_for_job) +
                        ", got " +
                        std::to_string(chunk_deals.size());

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                // ---------------------------------------------------------
                // Serialize the actual Deal objects into the FMTM payload.
                // ---------------------------------------------------------

                const std::string payload =
                    serialize_fmtm_payload(
                        inputPath,
                        fmtm_out_path,
                        errorPath,
                        calibratedMarket,
                        sessionId,
                        static_cast<std::size_t>(row_group),
                        static_cast<std::size_t>(
                            deal_count_for_job),
                        progressCounterKey,
                        bookId,
                        taskId,
                        reconciliation,
                        chunk_deals,
                        requestRetries);

                //
                // SQS maximum message size is 256 KiB.
                //
                if (payload.size() >
                    kMaxSqsMessageSize)
                {
                    const std::string msg =
                        "XDEAdapter: FMTM payload exceeds SQS "
                        "maximum message size. Row group=" +
                        std::to_string(row_group) +
                        ", deal index=" +
                        std::to_string(deal_index) +
                        ", deal count=" +
                        std::to_string(deal_count_for_job) +
                        ", payload size=" +
                        std::to_string(payload.size()) +
                        " bytes, maximum=" +
                        std::to_string(kMaxSqsMessageSize) +
                        " bytes";

                    mtoapi::MtoLogger::log(
                        mtoapi::LogLevel::error,
                        msg);

                    pushErrorLog(msg);

                    return {
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        make_result_json(
                            "error",
                            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                            msg)
                    };
                }

                mtoapi::MtoLogger::log(
                    mtoapi::LogLevel::debug,
                    "XDEAdapter: Created FMTM payload: "
                    "row_group=" +
                    std::to_string(row_group) +
                    ", deal_index=" +
                    std::to_string(deal_index) +
                    ", deal_count=" +
                    std::to_string(deal_count_for_job) +
                    ", payload_size=" +
                    std::to_string(payload.size()));

                //
                // Store payload and expected deal count.
                //
                all_payloads.push_back(payload);

                job_deal_counts.push_back(
                    static_cast<int>(
                        deal_count_for_job));
            }
            catch (const DealDecompositorException& e)
            {
                const std::string msg =
                    "XDEAdapter: Exception during deal decomposition "
                    "in row group " +
                    std::to_string(row_group) +
                    ", deal index " +
                    std::to_string(deal_index) +
                    ": " +
                    e.what();

                mtoapi::MtoLogger::log(
                    mtoapi::LogLevel::error,
                    msg);

                pushErrorLog(msg);

                return {
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    make_result_json(
                        "error",
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        msg)
                };
            }
            catch (const nlohmann::json::exception& e)
            {
                const std::string msg =
                    "XDEAdapter: Failed to serialize FMTM payload "
                    "in row group " +
                    std::to_string(row_group) +
                    ", deal index " +
                    std::to_string(deal_index) +
                    ": " +
                    e.what();

                mtoapi::MtoLogger::log(
                    mtoapi::LogLevel::error,
                    msg);

                pushErrorLog(msg);

                return {
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    make_result_json(
                        "error",
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        msg)
                };
            }
            catch (const std::exception& e)
            {
                const std::string msg =
                    "XDEAdapter: Exception while creating FMTM "
                    "payload in row group " +
                    std::to_string(row_group) +
                    ", deal index " +
                    std::to_string(deal_index) +
                    ": " +
                    e.what();

                mtoapi::MtoLogger::log(
                    mtoapi::LogLevel::error,
                    msg);

                pushErrorLog(msg);

                return {
                    QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                    make_result_json(
                        "error",
                        QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                        msg)
                };
            }
        }
    }

    //
    // Final validation: total generated jobs must account for all deals.
    //
    int64_t generated_deal_count = 0;

    for (const int count : job_deal_counts)
    {
        generated_deal_count += count;
    }

    if (generated_deal_count != deal_count)
    {
        const std::string msg =
            "XDEAdapter: Generated job deal count mismatch: "
            "expected=" +
            std::to_string(deal_count) +
            ", generated=" +
            std::to_string(generated_deal_count);

        mtoapi::MtoLogger::log(
            mtoapi::LogLevel::error,
            msg);

        pushErrorLog(msg);

        return {
            QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
            make_result_json(
                "error",
                QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,
                msg)
        };
    }

    return {
        QL_ADAPTER_SUCCESS,
        ""
    };
}



ANGEL: ConnectorSend()
-----------

std::optional<ProgressPublisher> progress_publisher;
std::vector<int> submitted_result_indices;
int total_expected_jobs = 0; // Set this if known upfront, or update dynamically

// Outer iteration over row groups
for (int row_group = 0; row_group < num_row_groups; ++row_group) {
    // ... Read row group table ...

    for (int64_t deal_index = 0; deal_index < deals_in_row_group; deal_index += static_cast<int64_t>(kDealsPerJob)) {
        
        const int deal_count_for_job = static_cast<int>(
            std::min<int64_t>(static_cast<int64_t>(kDealsPerJob), deals_in_row_group - deal_index));

        // --- 1. Slice and Decompose Deals ---
        auto sliced_table = full_rg_table->Slice(deal_index, deal_count_for_job);
        // ... (Decomposition logic to generate chunk_deals) ...

        // --- 2. Serialize Payload for Immediate Chunk ---
        const std::string payload = serialize_fmtm_payload(
            inputPath, fmtm_out_path, errorPath, calibratedMarket, sessionId,
            static_cast<std::size_t>(row_group),
            static_cast<std::size_t>(deal_count_for_job),
            progressCounterKey, bookId, taskId, reconciliation,
            chunk_deals, requestRetries);

        // Validation against SQS payload cap
        if (payload.size() > kMaxSqsMessageSize) {
            const std::string msg = "XDEAdapter: FMTM payload exceeds SQS maximum message size. Row group=" +
                std::to_string(row_group) + ", deal index=" + std::to_string(deal_index) +
                ", payload size=" + std::to_string(payload.size()) + " bytes";
            pushErrorLog(msg);
            return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
        }

        // --- 3. STREAMING DISPATCH: Send chunk immediately ---
        const std::vector<std::string> chunk_payload_vec = { payload };
        int result_index = xde::ConnectorSend(chunk_payload_vec, sessionId);

        if (result_index < 0) {
            const std::string msg = "XDEAdapter: Grid send failed for chunk (row group=" +
                std::to_string(row_group) + ", deal index=" + std::to_string(deal_index) + ")";
            mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
            total_send_failures += 1;
            aggregator.record_batch_result(0, deal_count_for_job, "xde-adapter");
            continue;
        }

        // Store result index for later collection
        submitted_result_indices.push_back(result_index);
        job_deal_counts.push_back(deal_count_for_job);

        // --- 4. INITIALIZE PROGRESS PUBLISHER (ON FIRST SUCCESSFUL SEND ONLY) ---
        if (!progress_publisher.has_value()) {
            std::string progress_s3_error;
            auto progress_s3 = make_progress_s3_client(progress_s3_error);
            if (!progress_s3) {
                mtoapi::MtoLogger::log(mtoapi::LogLevel::error, 
                    "XDEAdapter: Progress raw S3 client initialization failed: " + progress_s3_error);
            } else {
                progress_publisher.emplace(
                    aggregator, resultBucket, progressKey,
                    sessionId, taskId,
                    progress_redis_ready ? progressCounterKey : "",
                    static_cast<int>(total_expected_jobs), // Pass total expected count
                    agg_cfg_progress_interval_sec,
                    std::move(progress_s3));
                
                progress_publisher->start();
                mtoapi::MtoLogger::log(mtoapi::LogLevel::info, "XDEAdapter: Progress publisher started successfully.");
            }
        }
    }
}

// --- PHASE 3: Stream/Collect Results ---
mtoapi::MtoLogger::log(mtoapi::LogLevel::info,
    "XDEAdapter: Waiting for " + std::to_string(submitted_result_indices.size()) +
    " grid chunk result batch(es)");

std::vector<ConnectorResult> all_results;

for (size_t i = 0; i < submitted_result_indices.size(); ++i) {
    int res_idx = submitted_result_indices[i];
    
    // Retrieve result per submitted chunk index
    auto chunk_results = xde::ConnectorGetResults("XVA", res_idx, timeout_sec);

    if (chunk_results.empty()) {
        mtoapi::MtoLogger::log(mtoapi::LogLevel::error, 
            "XDEAdapter: No results returned for chunk result_index=" + std::to_string(res_idx));
        // Handle timeout / grid chunk failure...
    } else {
        all_results.insert(
            all_results.end(),
            std::make_move_iterator(chunk_results.begin()),
            std::make_move_iterator(chunk_results.end()));
    }
}
