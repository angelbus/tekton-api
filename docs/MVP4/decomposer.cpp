std::vector<Deal> DealDecompositor::DecomposeRecordBatch(
    const std::shared_ptr<arrow::RecordBatch>& batch) {
  if (!batch) {
    throw DealDecompositorException("Arrow RecordBatch is null.");
  }

  const int64_t row_count = batch->num_rows();

  if (row_count == 0) {
    throw DealDecompositorException("Arrow RecordBatch has no rows.");
  }
  // Arrow schema is defined by contract:
  // [0]dealId
  // [1] nettingSet
  // [2] csa
  // [3] trade
  // [4] legalStructureContext
  // [5] curency
  //
  // Kacpet: Column names/types shuould validated once during input metadata
  // validation. This method intentionally relies on the fixed
  // contract because it is executed for every FMTM chunk.

  constexpr int deal_id_column = 0;
  constexpr int netting_column = 1;
  constexpr int csa_column = 2;
  constexpr int trade_column = 3;
  constexpr int legal_column = 4;
  constexpr int currency_column = 5;

  constexpr int required_column_count = 6;

  if (batch->num_columns() < required_column_count) {
    throw DealDecompositorException(
        "Arrow RecordBatch does not contain the required columns. "
        "Expected at least " +
        std::to_string(required_column_count) +
        " columns, got " +
        std::to_string(batch->num_columns()) +
        ".");
  }

  // All columns are String according to the XENA input contract.
  //
  // The schema has already been validated when the Book metadata was
  // inspected, so no column-name lookup or type discovery is required
  // here.
  const auto deal_id_array = std::static_pointer_cast<arrow::StringArray>( batch->column(deal_id_column));
  const auto netting_array = std::static_pointer_cast<arrow::StringArray>( batch->column(netting_column));
  const auto csa_array = std::static_pointer_cast<arrow::StringArray>( batch->column(csa_column));
  const auto trade_array = std::static_pointer_cast<arrow::StringArray>( batch->column(trade_column));
  const auto legal_array = std::static_pointer_cast<arrow::StringArray>( batch->column(legal_column));
  const auto currency_array = std::static_pointer_cast<arrow::StringArray>( batch->column(currency_column));
  std::vector<Deal> deals;
  deals.reserve(static_cast<std::size_t>(row_count));

  for (int64_t row_idx = 0; row_idx < row_count; ++row_idx) {
    const std::string name = deal_id_array->IsNull(row_idx) ? "" : deal_id_array->GetString(row_idx);
    const std::string trade = trade_array->IsNull(row_idx)? "": trade_array->GetString(row_idx);
    const std::string collateral = csa_array->IsNull(row_idx)? "": csa_array->GetString(row_idx);
    const std::string currency = currency_array->IsNull(row_idx) ? "" : currency_array->GetString(row_idx);
    const std::string netting_set = netting_array->IsNull(row_idx) ? "" : netting_array->GetString(row_idx);
    const std::string csa = csa_array->IsNull(row_idx) ? "" : csa_array->GetString(row_idx);
    const std::string legal_structure_context = legal_array->IsNull(row_idx) ? "" : legal_array->GetString(row_idx);

    deals.emplace_back(
        name,
        trade,
        collateral,
        currency,
        netting_set,
        csa,
        legal_structure_context);
  }

  deal_count_ = deals.size();

  return deals;
}


------

if (batches.empty()) {
    const std::string msg = "FMTMAdapter: Arrow IPC stream contains no RecordBatch.";
    mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
    pushErrorLog(msg);
    return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json( "error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, msg)};
}

if (batches.size() != 1) {
    const std::string msg = "FMTMAdapter: Expected exactly one RecordBatch, got " + std::to_string(batches.size());
    mtoapi::MtoLogger::log(mtoapi::LogLevel::error, msg);
    pushErrorLog(msg);
    return {QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED, make_result_json("error", QL_ADAPTER_ERR_DEAL_DECOMPOSITION_FAILED,msg)¡};
}

// Decompose Arrow RecordBatch directly into deals.
deals = deal_decompositor_.DecomposeRecordBatch(batches.front());
