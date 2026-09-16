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
    const std::optional<int>& retries,
    const std::string& embedded_row_group_data) 
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
        std::string embedded_slice_data =
            mac_base64_encode(raw_bytes);

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
            requestRetries,
            embedded_slice_data));

        job_deal_counts.push_back(deal_count_for_job);
    }
}
