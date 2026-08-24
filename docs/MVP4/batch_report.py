import argparse
import json
import os
import tempfile
from collections import Counter

import boto3
import pandas as pd

from openpyxl import load_workbook
from openpyxl.chart import BarChart, PieChart, DoughnutChart, Reference
from openpyxl.chart.label import DataLabelList
from openpyxl.styles import Font, PatternFill, Alignment
from openpyxl.utils import get_column_letter
from openpyxl.worksheet.table import Table, TableStyleInfo


BUCKET = "xena-data-lake-output-dev"
MANIFEST_NAME = "manifest.json"
REPORT_NAME = "batch_report.xlsx"


# ============================================================
# Helpers
# ============================================================

def normalize_prefix(prefix):
    prefix = prefix.strip("/")
    return prefix + "/"


def get_book_folders(s3_client, prefix):
    """
    Get immediate child folders under the contract prefix.

    Example:

        fmtm/ES/DAILY_BATCH/20260811/0/5/

    returns:

        .../bookA/
        .../bookB/
        .../bookC/
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
    Read and deserialize manifest.json directly from S3.
    """

    response = s3_client.get_object(
        Bucket=BUCKET,
        Key=key
    )

    content = response["Body"].read()

    return json.loads(content.decode("utf-8"))


def safe_number(value, default=0):
    """
    Convert numeric values safely.
    """

    if value is None:
        return default

    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def flatten_value(value):
    """
    Keep complex JSON values as JSON text so that every
    top-level manifest field can still be represented in Excel.
    """

    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False)

    return value


def build_book_record(manifest, manifest_key):
    """
    Convert a manifest into one Excel row.
    """

    record = {}

    # --------------------------------------------------------
    # Keep ALL top-level manifest fields
    # --------------------------------------------------------

    for key, value in manifest.items():
        record[key] = flatten_value(value)

    # --------------------------------------------------------
    # Explicitly extract important fields
    # --------------------------------------------------------

    duration = safe_number(
        manifest.get("durationSeconds")
    )

    xena_seconds = safe_number(
        manifest.get("xenaSeconds")
    )

    grid_http_seconds = safe_number(
        manifest.get("gridHTTPSeconds")
    )

    # User specifically requested this calculation:
    #
    # durationSeconds - xenaSeconds - gridHTTPSeconds
    #
    calculated_processing = (
        duration
        - xena_seconds
        - grid_http_seconds
    )

    # Protect against tiny negative values caused by
    # floating-point precision.
    calculated_processing = max(
        0,
        calculated_processing
    )

    record["calculatedProcessingSeconds"] = calculated_processing

    # Useful source information
    record["manifestS3Key"] = manifest_key

    # Book identifier fallback
    if not record.get("book_id"):
        record["book_id"] = (
            record.get("bookId")
            or os.path.basename(
                os.path.dirname(manifest_key.rstrip("/"))
            )
        )

    return record


# ============================================================
# Excel formatting
# ============================================================

def format_sheet(ws):
    """
    Basic formatting for an Excel worksheet.
    """

    ws.freeze_panes = "A2"
    ws.auto_filter.ref = ws.dimensions

    for cell in ws[1]:
        cell.font = Font(
            bold=True
        )

        cell.alignment = Alignment(
            horizontal="center"
        )

    # Adjust column widths
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
    """
    Convert worksheet data into an Excel Table.
    """

    if ws.max_row < 2:
        return

    ref = ws.dimensions

    table = Table(
        displayName=table_name,
        ref=ref
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
    """
    Generate the Excel report.
    """

    if not records:
        raise RuntimeError(
            "No manifest.json files were found."
        )

    df = pd.DataFrame(records)

    # --------------------------------------------------------
    # Normalize important numeric columns
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
        "retries",
        "gridHTTPSseconds"
    ]

    for column in numeric_columns:

        if column in df.columns:
            df[column] = pd.to_numeric(
                df[column],
                errors="coerce"
            ).fillna(0)

    # --------------------------------------------------------
    # Normalize boolean fields
    # --------------------------------------------------------

    for column in [
        "reconciliation"
    ]:
        if column in df.columns:
            df[column] = df[column].astype(str)

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
            (status_series == "COMPLETED").sum()
        )

        failed_books = int(
            (status_series == "FAILED").sum()
        )

    summary_rows = []

    def add_summary(metric, value, unit=""):
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

    if total_books:
        add_summary(
            "Success Rate",
            completed_books / total_books,
            "%"
        )

    if "totalItems" in df:
        add_summary(
            "Total Items / Deals",
            df["totalItems"].sum(),
            "items"
        )

    if "processedItems" in df:
        add_summary(
            "Total Processed Items",
            df["processedItems"].sum(),
            "items"
        )

    if "failedItems" in df:
        add_summary(
            "Total Failed Items",
            df["failedItems"].sum(),
            "items"
        )

    if "totalJobs" in df:
        add_summary(
            "Total GRID Jobs",
            df["totalJobs"].sum(),
            "jobs"
        )

    if "timeouts" in df:
        add_summary(
            "Total Timeouts",
            df["timeouts"].sum(),
            "timeouts"
        )

    if "retries" in df:
        add_summary(
            "Total Retries",
            df["retries"].sum(),
            "retries"
        )

    if "durationSeconds" in df:
        add_summary(
            "Total Duration",
            df["durationSeconds"].sum(),
            "seconds"
        )

    if "xenaSeconds" in df:
        add_summary(
            "Total XENA Time",
            df["xenaSeconds"].sum(),
            "seconds"
        )

    if "gridHTTPSeconds" in df:
        add_summary(
            "Total GRID HTTP Time",
            df["gridHTTPSeconds"].sum(),
            "seconds"
        )

    if "calculatedProcessingSeconds" in df:
        add_summary(
            "Total Calculated Processing Time",
            df["calculatedProcessingSeconds"].sum(),
            "seconds"
        )

    # --------------------------------------------------------
    # Aggregation by important dimensions
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

        grouped = (
            df.groupby(dimension, dropna=False)
            .agg(
                Books=("book_id", "count"),
                TotalItems=("totalItems", "sum")
                    if "totalItems" in df.columns
                    else ("book_id", "count"),
                TotalJobs=("totalJobs", "sum")
                    if "totalJobs" in df.columns
                    else ("book_id", "count"),
                TotalDurationSeconds=(
                    "durationSeconds", "sum"
                )
                    if "durationSeconds" in df.columns
                    else ("book_id", "count"),
                TotalXenaSeconds=(
                    "xenaSeconds", "sum"
                )
                    if "xenaSeconds" in df.columns
                    else ("book_id", "count"),
                TotalGridHTTPSeconds=(
                    "gridHTTPSeconds", "sum"
                )
                    if "gridHTTPSeconds" in df.columns
                    else ("book_id", "count"),
                TotalProcessingSeconds=(
                    "calculatedProcessingSeconds", "sum"
                )
                    if "calculatedProcessingSeconds" in df.columns
                    else ("book_id", "count")
            )
            .reset_index()
        )

        aggregation_sheets[
            dimension
        ] = grouped

    # --------------------------------------------------------
    # Create workbook
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

        for sheet_name, data in aggregation_sheets.items():

            # Excel sheet names have a 31-character limit
            safe_sheet_name = (
                sheet_name[:31]
            )

            data.to_excel(
                writer,
                sheet_name=safe_sheet_name,
                index=False
            )

    # --------------------------------------------------------
    # Re-open workbook to add formatting/charts
    # --------------------------------------------------------

    wb = load_workbook(output_file)

    # --------------------------------------------------------
    # Books sheet
    # --------------------------------------------------------

    ws_books = wb["Books"]

    format_sheet(ws_books)
    add_excel_table(
        ws_books,
        "BooksTable"
    )

    # --------------------------------------------------------
    # Summary sheet
    # --------------------------------------------------------

    ws_summary = wb["Summary"]

    format_sheet(ws_summary)

    # --------------------------------------------------------
    # Charts sheet
    # --------------------------------------------------------

    ws_charts = wb.create_sheet(
        "Charts"
    )

    ws_charts["A1"] = "XENA Batch Report"
    ws_charts["A1"].font = Font(
        bold=True,
        size=18
    )

    ws_charts["A3"] = "Books Status"

    ws_charts["A4"] = "Status"
    ws_charts["B4"] = "Books"

    ws_charts["A5"] = "Completed"
    ws_charts["B5"] = completed_books

    ws_charts["A6"] = "Failed"
    ws_charts["B6"] = failed_books

    # --------------------------------------------------------
    # Chart 1 - Completed vs Failed Books
    # --------------------------------------------------------

    status_chart = DoughnutChart()

    labels = Reference(
        ws_charts,
        min_col=1,
        min_row=5,
        max_row=6
    )

    data = Reference(
        ws_charts,
        min_col=2,
        min_row=4,
        max_row=6
    )

    status_chart.add_data(
        data,
        titles_from_data=True
    )

    status_chart.set_categories(
        labels
    )

    status_chart.title = (
        "Completed vs Failed Books"
    )

    status_chart.dataLabels = DataLabelList()
    status_chart.dataLabels.showPercent = True

    ws_charts.add_chart(
        status_chart,
        "D3"
    )

    # --------------------------------------------------------
    # Chart 2 - Time Breakdown
    # --------------------------------------------------------

    time_row = 10

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

    total_xena = (
        df["xenaSeconds"].sum()
        if "xenaSeconds" in df
        else 0
    )

    total_grid = (
        df["gridHTTPSeconds"].sum()
        if "gridHTTPSeconds" in df
        else 0
    )

    total_processing = (
        df["calculatedProcessingSeconds"].sum()
        if "calculatedProcessingSeconds" in df
        else 0
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

    time_chart.y_axis.title = "Seconds"

    ws_charts.add_chart(
        time_chart,
        "D15"
    )

    # --------------------------------------------------------
    # Distribution helper
    # --------------------------------------------------------

    def create_distribution(
        source_df,
        column,
        title,
        start_row,
        start_col
    ):

        if column not in source_df.columns:
            return None

        values = pd.to_numeric(
            source_df[column],
            errors="coerce"
        ).dropna()

        if values.empty:
            return None

        # Five practical ranges
        maximum = values.max()

        if maximum <= 10:
            bins = [
                0, 1, 2, 5, 10,
                float("inf")
            ]
        elif maximum <= 100:
            bins = [
                0, 10, 25, 50, 75,
                100, float("inf")
            ]
        else:
            bins = [
                0, 10, 50, 100, 500,
                1000, 5000,
                10000, float("inf")
            ]

        labels = []

        for i in range(len(bins) - 1):

            lower = bins[i]
            upper = bins[i + 1]

            if upper == float("inf"):
                label = f"{int(lower)}+"
            elif lower == 0:
                label = f"0-{int(upper)}"
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
            max_row=start_row + 1 + len(counts)
        )

        categories = Reference(
            ws_charts,
            min_col=start_col,
            min_row=start_row + 2,
            max_row=start_row + 1 + len(counts)
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
            f"{get_column_letter(start_col + 2)}{start_row}"
        )

    # --------------------------------------------------------
    # Chart 3 - Items / Deals per Book
    # --------------------------------------------------------

    create_distribution(
        df,
        "totalItems",
        "Distribution of Items / Deals per Book",
        30,
        1
    )

    # --------------------------------------------------------
    # Chart 4 - GRID Jobs per Book
    # --------------------------------------------------------

    create_distribution(
        df,
        "totalJobs",
        "Distribution of GRID Jobs per Book",
        30,
        7
    )

    # --------------------------------------------------------
    # Chart 5 - Row Groups per Book
    # --------------------------------------------------------

    create_distribution(
        df,
        "totalRowGroups",
        "Distribution of Row Groups per Book",
        30,
        13
    )

    # --------------------------------------------------------
    # Chart 6 - Items per Job
    # --------------------------------------------------------

    create_distribution(
        df,
        "itemsPerJob",
        "Distribution of Items per GRID Job",
        30,
        19
    )

    # --------------------------------------------------------
    # General chart sheet formatting
    # --------------------------------------------------------

    ws_charts.column_dimensions["A"].width = 30
    ws_charts.column_dimensions["B"].width = 18
    ws_charts.column_dimensions["G"].width = 30
    ws_charts.column_dimensions["M"].width = 30
    ws_charts.column_dimensions["S"].width = 30

    # --------------------------------------------------------
    # Save
    # --------------------------------------------------------

    wb.save(output_file)


# ============================================================
# Main
# ============================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Generate an Excel batch report from "
            "XENA manifest.json files."
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
    print(f"Bucket : s3://{BUCKET}/")
    print(f"Prefix : {prefix}")
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
    # Process each Book
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
                    f"    ERROR reading manifest: "
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
    # Generate Excel report
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
        f"Report generated successfully."
    )

    # --------------------------------------------------------
    # Upload report to same contract
    # --------------------------------------------------------

    report_key = (
        prefix.rstrip("/")
        + "/"
        + os.path.basename(args.output)
    )

    print()
    print(
        f"Uploading report to:"
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
        f"S3 report    : s3://{BUCKET}/{report_key}"
    )


if __name__ == "__main__":
    main()


#python batch_report.py \
#    "fmtm/ES/DAILY_BATCH/20260811/0/5/"
