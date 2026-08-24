import argparse
import json
import os

import boto3
import pandas as pd

from openpyxl import load_workbook
from openpyxl.chart import BarChart, DoughnutChart, Reference
from openpyxl.chart.label import DataLabelList
from openpyxl.styles import Font, Alignment
from openpyxl.utils import get_column_letter
from openpyxl.worksheet.table import Table, TableStyleInfo


BUCKET = "xena-data-lake-output-dev"
MANIFEST_NAME = "manifest.json"
REPORT_NAME = "batch_report.xlsx"

# Defaults used by the XENA manifest/report logic
DEFAULT_XENA_SECONDS = 1.138
DEFAULT_GRID_HTTP_SECONDS = 2.17


# ============================================================
# Helpers
# ============================================================

def normalize_prefix(prefix):
    prefix = prefix.strip("/")
    return prefix + "/"


def get_book_folders(s3_client, prefix):
    """
    Get immediate child folders under the contract prefix.
    """

    folders = []

    paginator = s3_client.get_paginator("list_objects_v2")

    for page in paginator.paginate(
        Bucket=BUCKET,
        Prefix=prefix,
        Delimiter="/"
    ):
        for common_prefix in page.get("CommonPrefixes", []):
            folders.append(common_prefix["Prefix"])

    return folders


def find_manifests(s3_client, folder_prefix):
    """
    Recursively find manifest.json under a Book folder.
    """

    manifests = []

    paginator = s3_client.get_paginator("list_objects_v2")

    for page in paginator.paginate(
        Bucket=BUCKET,
        Prefix=folder_prefix
    ):
        for obj in page.get("Contents", []):
            key = obj["Key"]

            if key.endswith("/" + MANIFEST_NAME):
                manifests.append(key)

    return manifests


def read_manifest(s3_client, key):
    """
    Read manifest.json directly from S3.
    """

    response = s3_client.get_object(
        Bucket=BUCKET,
        Key=key
    )

    content = response["Body"].read()

    return json.loads(
        content.decode("utf-8")
    )


def safe_number(value, default=0):
    """
    Convert a value to a number safely.
    """

    if value is None:
        return default

    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def flatten_value(value):
    """
    Keep complex JSON values as JSON text.
    """

    if isinstance(value, (dict, list)):
        return json.dumps(
            value,
            ensure_ascii=False
        )

    return value


# ============================================================
# Manifest -> Excel row
# ============================================================

def build_book_record(manifest, manifest_key):
    """
    Convert one manifest into one Excel row.

    All original top-level manifest fields are preserved.
    Missing calculated/default fields are populated according
    to the XENA defaults.
    """

    record = {}

    # --------------------------------------------------------
    # Preserve ALL manifest fields
    # --------------------------------------------------------

    for key, value in manifest.items():
        record[key] = flatten_value(value)

    # --------------------------------------------------------
    # Defaults
    # --------------------------------------------------------

    # xenaSeconds:
    # default to 1.138 if not present in manifest.json
    xena_seconds = safe_number(
        manifest.get(
            "xenaSeconds",
            DEFAULT_XENA_SECONDS
        ),
        DEFAULT_XENA_SECONDS
    )

    # gridHTTPSeconds:
    # default to 2.17 if not present in manifest.json
    grid_http_seconds = safe_number(
        manifest.get(
            "gridHTTPSeconds",
            DEFAULT_GRID_HTTP_SECONDS
        ),
        DEFAULT_GRID_HTTP_SECONDS
    )

    # totalItems:
    # default to processedItems + failedItems
    if "totalItems" in manifest:
        total_items = safe_number(
            manifest["totalItems"]
        )
    else:
        total_items = (
            safe_number(
                manifest.get("processedItems", 0)
            )
            +
            safe_number(
                manifest.get("failedItems", 0)
            )
        )

    total_jobs = safe_number(
        manifest.get("totalJobs", 0)
    )

    # itemsPerJob:
    # default to totalItems / totalJobs
    if "itemsPerJob" in manifest:
        items_per_job = safe_number(
            manifest["itemsPerJob"]
        )
    elif total_jobs > 0:
        items_per_job = (
            total_items / total_jobs
        )
    else:
        items_per_job = 0

    # --------------------------------------------------------
    # Duration calculations
    # --------------------------------------------------------

    duration_seconds = safe_number(
        manifest.get("durationSeconds", 0)
    )

    processing_seconds = safe_number(
        manifest.get("processingSeconds", 0)
    )

    calculated_processing_seconds = (
        duration_seconds
        - xena_seconds
        - grid_http_seconds
    )

    # Avoid tiny negative values caused by precision.
    calculated_processing_seconds = max(
        0,
        calculated_processing_seconds
    )

    # --------------------------------------------------------
    # Explicit calculated/defaulted fields
    # --------------------------------------------------------

    record["xenaSeconds"] = xena_seconds
    record["gridHTTPSeconds"] = grid_http_seconds
    record["totalItems"] = total_items
    record["itemsPerJob"] = items_per_job

    record["calculatedProcessingSeconds"] = (
        calculated_processing_seconds
    )

    # Keep original processingSeconds if present.
    record["processingSeconds"] = (
        processing_seconds
    )

    # Useful source information
    record["manifestS3Key"] = manifest_key

    # --------------------------------------------------------
    # Book ID fallback
    # --------------------------------------------------------

    if not record.get("book_id"):
        record["book_id"] = (
            record.get("bookId")
            or os.path.basename(
                os.path.dirname(
                    manifest_key.rstrip("/")
                )
            )
        )

    return record


# ============================================================
# Excel formatting
# ============================================================

def format_sheet(ws):

    ws.freeze_panes = "A2"

    for cell in ws[1]:
        cell.font = Font(
            bold=True
        )

        cell.alignment = Alignment(
            horizontal="center"
        )

    for column_cells in ws.columns:

        max_length = 0

        column_letter = get_column_letter(
            column_cells[0].column
        )

        for cell in column_cells:

            if cell.value is not None:
                max_length = max(
                    max_length,
                    len(str(cell.value))
                )

        ws.column_dimensions[
            column_letter
        ].width = min(
            max(max_length + 2, 12),
            45
        )


def add_excel_table(ws, table_name):

    if ws.max_row < 2:
        return

    table = Table(
        displayName=table_name,
        ref=ws.dimensions
    )

    style = TableStyleInfo(
        name="TableStyleMedium2",
        showFirstColumn=False,
        showLastColumn=False,
        showRowStripes=True,
        showColumnStripes=False
    )

    table.tableStyleInfo = style

    ws.add_table(table)


# ============================================================
# Report generation
# ============================================================

def create_report(records, output_file):

    if not records:
        raise RuntimeError(
            "No manifest.json files were found."
        )

    df = pd.DataFrame(records)

    # --------------------------------------------------------
    # Numeric columns
    # --------------------------------------------------------

    numeric_columns = [
        "durationSeconds",
        "xenaSeconds",
        "gridHTTPSeconds",
        "processingSeconds",
        "calculatedProcessingSeconds",
        "totalItems",
        "processedItems",
        "failedItems",
        "totalJobs",
        "itemsPerJob",
        "totalRowGroups",
        "timeouts",
        "retries"
    ]

    for column in numeric_columns:

        if column in df.columns:
            df[column] = pd.to_numeric(
                df[column],
                errors="coerce"
            ).fillna(0)

    # --------------------------------------------------------
    # Summary
    # --------------------------------------------------------

    total_books = len(df)

    completed_books = 0
    failed_books = 0

    if "status" in df.columns:

        status_series = (
            df["status"]
            .astype(str)
            .str.upper()
        )

        completed_books = int(
            (
                status_series == "COMPLETED"
            ).sum()
        )

        failed_books = int(
            (
                status_series == "FAILED"
            ).sum()
        )

    # --------------------------------------------------------
    # Deal-level success
    # --------------------------------------------------------

    total_items = (
        df["totalItems"].sum()
        if "totalItems" in df.columns
        else 0
    )

    processed_items = (
        df["processedItems"].sum()
        if "processedItems" in df.columns
        else 0
    )

    failed_items = (
        df["failedItems"].sum()
        if "failedItems" in df.columns
        else 0
    )

    if total_items > 0:
        deal_success_rate = (
            processed_items / total_items
        )
    else:
        deal_success_rate = 0

    # --------------------------------------------------------
    # Total batch duration
    #
    # Actual wall-clock batch duration:
    #
    # MAX(endTime) - MIN(startTime)
    # --------------------------------------------------------

    batch_duration_seconds = 0
    batch_duration_formatted = "00:00"

    if (
        "startTime" in df.columns
        and "endTime" in df.columns
    ):

        start_times = pd.to_datetime(
            df["startTime"],
            errors="coerce",
            utc=True
        )

        end_times = pd.to_datetime(
            df["endTime"],
            errors="coerce",
            utc=True
        )

        valid_starts = start_times.dropna()
        valid_ends = end_times.dropna()

        if (
            not valid_starts.empty
            and not valid_ends.empty
        ):

            earliest_start = valid_starts.min()
            latest_end = valid_ends.max()

            batch_duration = (
                latest_end - earliest_start
            )

            batch_duration_seconds = (
                batch_duration.total_seconds()
            )

            total_minutes = int(
                batch_duration_seconds // 60
            )

            remaining_seconds = int(
                batch_duration_seconds % 60
            )

            batch_duration_formatted = (
                f"{total_minutes:02d}:"
                f"{remaining_seconds:02d}"
            )

    # --------------------------------------------------------
    # Summary rows
    # --------------------------------------------------------

    summary_rows = []

    def add_summary(
        metric,
        value,
        unit=""
    ):
        summary_rows.append({
            "Metric": metric,
            "Value": value,
            "Unit": unit
        })

    add_summary(
        "Total Books",
        total_books,
        "books"
    )

    add_summary(
        "Completed Books",
        completed_books,
        "books"
    )

    add_summary(
        "Failed Books",
        failed_books,
        "books"
    )

    add_summary(
        "Book Success Rate",
        (
            completed_books / total_books
            if total_books
            else 0
        ),
        "%"
    )

    add_summary(
        "Total Deals / Items",
        total_items,
        "items"
    )

    add_summary(
        "Completed Deals / Items",
        processed_items,
        "items"
    )

    add_summary(
        "Failed Deals / Items",
        failed_items,
        "items"
    )

    add_summary(
        "Success Rate per Deals",
        deal_success_rate,
        "%"
    )

    add_summary(
        "Total GRID Jobs",
        (
            df["totalJobs"].sum()
            if "totalJobs" in df.columns
            else 0
        ),
        "jobs"
    )

    add_summary(
        "Total Row Groups",
        (
            df["totalRowGroups"].sum()
            if "totalRowGroups" in df.columns
            else 0
        ),
        "row groups"
    )

    add_summary(
        "Total Timeouts",
        (
            df["timeouts"].sum()
            if "timeouts" in df.columns
            else 0
        ),
        "timeouts"
    )

    add_summary(
        "Total Retries",
        (
            df["retries"].sum()
            if "retries" in df.columns
            else 0
        ),
        "retries"
    )

    add_summary(
        "Total Batch Duration",
        batch_duration_formatted,
        "MM:SS"
    )

    add_summary(
        "Total Batch Duration Seconds",
        batch_duration_seconds,
        "seconds"
    )

    # --------------------------------------------------------
    # Time totals
    # --------------------------------------------------------

    total_duration = (
        df["durationSeconds"].sum()
        if "durationSeconds" in df.columns
        else 0
    )

    total_xena = (
        df["xenaSeconds"].sum()
        if "xenaSeconds" in df.columns
        else 0
    )

    total_grid = (
        df["gridHTTPSeconds"].sum()
        if "gridHTTPSeconds" in df.columns
        else 0
    )

    total_processing = (
        df["calculatedProcessingSeconds"].sum()
        if "calculatedProcessingSeconds" in df.columns
        else 0
    )

    add_summary(
        "Sum of Book Durations",
        total_duration,
        "seconds"
    )

    add_summary(
        "Total XENA Time",
        total_xena,
        "seconds"
    )

    add_summary(
        "Total GRID HTTP Time",
        total_grid,
        "seconds"
    )

    add_summary(
        "Total Calculated Processing Time",
        total_processing,
        "seconds"
    )

    # --------------------------------------------------------
    # Aggregations
    # --------------------------------------------------------

    aggregation_sheets = {}

    for dimension in [
        "geography",
        "purpose",
        "subPurpose",
        "iteration",
        "status",
        "job_type"
    ]:

        if dimension not in df.columns:
            continue

        grouped_data = {
            "Books": (
                "book_id",
                "count"
            )
        }

        if "totalItems" in df.columns:
            grouped_data[
                "TotalItems"
            ] = (
                "totalItems",
                "sum"
            )

        if "totalJobs" in df.columns:
            grouped_data[
                "TotalJobs"
            ] = (
                "totalJobs",
                "sum"
            )

        if "durationSeconds" in df.columns:
            grouped_data[
                "TotalDurationSeconds"
            ] = (
                "durationSeconds",
                "sum"
            )

        if "xenaSeconds" in df.columns:
            grouped_data[
                "TotalXenaSeconds"
            ] = (
                "xenaSeconds",
                "sum"
            )

        if "gridHTTPSeconds" in df.columns:
            grouped_data[
                "TotalGridHTTPSeconds"
            ] = (
                "gridHTTPSeconds",
                "sum"
            )

        if "calculatedProcessingSeconds" in df.columns:
            grouped_data[
                "TotalProcessingSeconds"
            ] = (
                "calculatedProcessingSeconds",
                "sum"
            )

        grouped = (
            df.groupby(
                dimension,
                dropna=False
            )
            .agg(**grouped_data)
            .reset_index()
        )

        aggregation_sheets[
            dimension
        ] = grouped

    # --------------------------------------------------------
    # Write initial workbook
    # --------------------------------------------------------

    with pd.ExcelWriter(
        output_file,
        engine="openpyxl"
    ) as writer:

        df.to_excel(
            writer,
            sheet_name="Books",
            index=False
        )

        pd.DataFrame(
            summary_rows
        ).to_excel(
            writer,
            sheet_name="Summary",
            index=False
        )

        for sheet_name, data in (
            aggregation_sheets.items()
        ):

            data.to_excel(
                writer,
                sheet_name=sheet_name[:31],
                index=False
            )

    # --------------------------------------------------------
    # Re-open workbook
    # --------------------------------------------------------

    wb = load_workbook(
        output_file
    )

    # --------------------------------------------------------
    # Books
    # --------------------------------------------------------

    ws_books = wb["Books"]

    format_sheet(ws_books)

    add_excel_table(
        ws_books,
        "BooksTable"
    )

    # --------------------------------------------------------
    # Summary
    # --------------------------------------------------------

    ws_summary = wb["Summary"]

    format_sheet(ws_summary)

    # Format percentage values
    for row in range(
        2,
        ws_summary.max_row + 1
    ):

        metric = ws_summary.cell(
            row,
            1
        ).value

        if metric in [
            "Book Success Rate",
            "Success Rate per Deals"
        ]:

            ws_summary.cell(
                row,
                2
            ).number_format = "0.00%"

    # --------------------------------------------------------
    # Charts sheet
    # --------------------------------------------------------

    ws_charts = wb.create_sheet(
        "Charts"
    )

    ws_charts["A1"] = (
        "XENA Batch Report"
    )

    ws_charts["A1"].font = Font(
        bold=True,
        size=18
    )

    # ========================================================
    # CHART 1
    # Books: Completed vs Failed
    # ========================================================

    ws_charts["A3"] = (
        "Books: Completed vs Failed"
    )

    ws_charts["A4"] = "Status"
    ws_charts["B4"] = "Books"

    ws_charts["A5"] = "Completed"
    ws_charts["B5"] = completed_books

    ws_charts["A6"] = "Failed"
    ws_charts["B6"] = failed_books

    book_chart = DoughnutChart()

    book_data = Reference(
        ws_charts,
        min_col=2,
        min_row=4,
        max_row=6
    )

    book_labels = Reference(
        ws_charts,
        min_col=1,
        min_row=5,
        max_row=6
    )

    book_chart.add_data(
        book_data,
        titles_from_data=True
    )

    book_chart.set_categories(
        book_labels
    )

    book_chart.title = (
        "Book Success"
    )

    book_chart.dataLabels = (
        DataLabelList()
    )

    book_chart.dataLabels.showPercent = True

    ws_charts.add_chart(
        book_chart,
        "D3"
    )

    # ========================================================
    # CHART 2
    # Deals / Items: Completed vs Failed
    # ========================================================

    ws_charts["A10"] = (
        "Deals / Items: Completed vs Failed"
    )

    ws_charts["A11"] = "Status"
    ws_charts["B11"] = "Deals / Items"

    ws_charts["A12"] = "Completed"
    ws_charts["B12"] = processed_items

    ws_charts["A13"] = "Failed"
    ws_charts["B13"] = failed_items

    deals_chart = DoughnutChart()

    deals_data = Reference(
        ws_charts,
        min_col=2,
        min_row=11,
        max_row=13
    )

    deals_labels = Reference(
        ws_charts,
        min_col=1,
        min_row=12,
        max_row=13
    )

    deals_chart.add_data(
        deals_data,
        titles_from_data=True
    )

    deals_chart.set_categories(
        deals_labels
    )

    deals_chart.title = (
        "Deal / Item Success"
    )

    deals_chart.dataLabels = (
        DataLabelList()
    )

    deals_chart.dataLabels.showPercent = True

    ws_charts.add_chart(
        deals_chart,
        "D18"
    )

    # ========================================================
    # CHART 3
    # Time Breakdown
    # ========================================================

    time_row = 32

    ws_charts.cell(
        time_row,
        1,
        "Time Component"
    )

    ws_charts.cell(
        time_row,
        2,
        "Seconds"
    )

    ws_charts.cell(
        time_row + 1,
        1,
        "XENA"
    )

    ws_charts.cell(
        time_row + 1,
        2,
        total_xena
    )

    ws_charts.cell(
        time_row + 2,
        1,
        "GRID HTTP"
    )

    ws_charts.cell(
        time_row + 2,
        2,
        total_grid
    )

    ws_charts.cell(
        time_row + 3,
        1,
        "Processing"
    )

    ws_charts.cell(
        time_row + 3,
        2,
        total_processing
    )

    time_chart = BarChart()

    time_data = Reference(
        ws_charts,
        min_col=2,
        min_row=time_row,
        max_row=time_row + 3
    )

    time_labels = Reference(
        ws_charts,
        min_col=1,
        min_row=time_row + 1,
        max_row=time_row + 3
    )

    time_chart.add_data(
        time_data,
        titles_from_data=True
    )

    time_chart.set_categories(
        time_labels
    )

    time_chart.title = (
        "Total Time Breakdown"
    )

    time_chart.y_axis.title = (
        "Seconds"
    )

    ws_charts.add_chart(
        time_chart,
        "D33"
    )

    # ========================================================
    # Distribution helper
    # ========================================================

    def create_distribution(
        source_df,
        column,
        title,
        start_row,
        start_col
    ):

        if column not in source_df.columns:
            return

        values = pd.to_numeric(
            source_df[column],
            errors="coerce"
        ).dropna()

        if values.empty:
            return

        maximum = values.max()

        if maximum <= 10:

            bins = [
                0,
                1,
                2,
                5,
                10,
                float("inf")
            ]

        elif maximum <= 100:

            bins = [
                0,
                10,
                25,
                50,
                75,
                100,
                float("inf")
            ]

        else:

            bins = [
                0,
                10,
                50,
                100,
                500,
                1000,
                5000,
                10000,
                float("inf")
            ]

        labels = []

        for i in range(
            len(bins) - 1
        ):

            lower = bins[i]
            upper = bins[i + 1]

            if upper == float("inf"):

                label = (
                    f"{int(lower)}+"
                )

            elif lower == 0:

                label = (
                    f"0-{int(upper)}"
                )

            else:

                label = (
                    f"{int(lower) + 1}-"
                    f"{int(upper)}"
                )

            labels.append(label)

        distribution = pd.cut(
            values,
            bins=bins,
            labels=labels,
            include_lowest=True
        )

        counts = (
            distribution
            .value_counts()
            .sort_index()
        )

        ws_charts.cell(
            start_row,
            start_col,
            title
        )

        ws_charts.cell(
            start_row + 1,
            start_col,
            "Range"
        )

        ws_charts.cell(
            start_row + 1,
            start_col + 1,
            "Books"
        )

        for index, (
            label,
            count
        ) in enumerate(
            counts.items(),
            start=start_row + 2
        ):

            ws_charts.cell(
                index,
                start_col,
                str(label)
            )

            ws_charts.cell(
                index,
                start_col + 1,
                int(count)
            )

        chart = BarChart()

        data = Reference(
            ws_charts,
            min_col=start_col + 1,
            min_row=start_row + 1,
            max_row=(
                start_row
                + 1
                + len(counts)
            )
        )

        categories = Reference(
            ws_charts,
            min_col=start_col,
            min_row=start_row + 2,
            max_row=(
                start_row
                + 1
                + len(counts)
            )
        )

        chart.add_data(
            data,
            titles_from_data=True
        )

        chart.set_categories(
            categories
        )

        chart.title = title
        chart.y_axis.title = "Books"

        ws_charts.add_chart(
            chart,
            f"{get_column_letter(start_col + 2)}"
            f"{start_row}"
        )

    # ========================================================
    # Distributions
    # ========================================================

    create_distribution(
        df,
        "totalItems",
        "Distribution of Items / Deals per Book",
        50,
        1
    )

    create_distribution(
        df,
        "totalJobs",
        "Distribution of GRID Jobs per Book",
        50,
        7
    )

    create_distribution(
        df,
        "totalRowGroups",
        "Distribution of Row Groups per Book",
        50,
        13
    )

    create_distribution(
        df,
        "itemsPerJob",
        "Distribution of Items per GRID Job",
        50,
        19
    )

    # --------------------------------------------------------
    # Formatting
    # --------------------------------------------------------

    ws_charts.column_dimensions[
        "A"
    ].width = 32

    ws_charts.column_dimensions[
        "B"
    ].width = 18

    ws_charts.column_dimensions[
        "G"
    ].width = 32

    ws_charts.column_dimensions[
        "M"
    ].width = 32

    ws_charts.column_dimensions[
        "S"
    ].width = 32

    wb.save(output_file)


# ============================================================
# Main
# ============================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Generate an Excel XENA batch report "
            "from manifest.json files."
        )
    )

    parser.add_argument(
        "prefix",
        help=(
            "XENA S3 contract prefix, e.g. "
            "fmtm/ES/DAILY_BATCH/20260811/0/5/"
        )
    )

    parser.add_argument(
        "--output",
        default=REPORT_NAME,
        help=(
            "Local report filename "
            "(default: batch_report.xlsx)"
        )
    )

    args = parser.parse_args()

    prefix = normalize_prefix(
        args.prefix
    )

    s3 = boto3.client("s3")

    print("=" * 70)
    print("XENA Batch Report")
    print("=" * 70)
    print(
        f"Bucket : s3://{BUCKET}/"
    )
    print(
        f"Prefix : {prefix}"
    )
    print()

    # --------------------------------------------------------
    # Find Book folders
    # --------------------------------------------------------

    folders = get_book_folders(
        s3,
        prefix
    )

    print(
        f"Book folders found: {len(folders)}"
    )

    print()

    records = []

    # --------------------------------------------------------
    # Process Books
    # --------------------------------------------------------

    for index, folder in enumerate(
        folders,
        start=1
    ):

        print(
            f"[{index}/{len(folders)}] "
            f"Processing: {folder}"
        )

        manifests = find_manifests(
            s3,
            folder
        )

        if not manifests:

            print(
                "    No manifest.json found."
            )

            continue

        for manifest_key in manifests:

            print(
                f"    Reading: {manifest_key}"
            )

            try:

                manifest = read_manifest(
                    s3,
                    manifest_key
                )

                record = build_book_record(
                    manifest,
                    manifest_key
                )

                records.append(
                    record
                )

            except Exception as exc:

                print(
                    "    ERROR reading manifest: "
                    f"{exc}"
                )

    print()

    print(
        f"Manifest records collected: "
        f"{len(records)}"
    )

    if not records:
        raise RuntimeError(
            "No valid manifest.json files found."
        )

    # --------------------------------------------------------
    # Generate report
    # --------------------------------------------------------

    print()

    print(
        f"Generating report: {args.output}"
    )

    create_report(
        records,
        args.output
    )

    print(
        "Report generated successfully."
    )

    # --------------------------------------------------------
    # Upload report
    # --------------------------------------------------------

    report_key = (
        prefix.rstrip("/")
        + "/"
        + os.path.basename(
            args.output
        )
    )

    print()

    print(
        "Uploading report to:"
    )

    print(
        f"s3://{BUCKET}/{report_key}"
    )

    s3.upload_file(
        args.output,
        BUCKET,
        report_key
    )

    print()

    print("=" * 70)
    print("Completed")
    print("=" * 70)

    print(
        f"Local report : {args.output}"
    )

    print(
        f"S3 report    : "
        f"s3://{BUCKET}/{report_key}"
    )


if __name__ == "__main__":
    main()
