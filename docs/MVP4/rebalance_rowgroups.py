#!/usr/bin/env python3
#
# rebalance_rowgroups.py
#
# Proof-of-Concept:
# Reads a Parquet file and rewrites it with homogeneous Row Groups.
#
# Requirements:
#   pip install pyarrow
#
# Usage:
#   python rebalance_rowgroups.py input.parquet output.parquet --parallelism 8
#

import argparse
import math
import time

import pyarrow as pa
import pyarrow.parquet as pq


# -------------------------------------------------------------

def print_row_groups(parquet_file):
    pf = pq.ParquetFile(parquet_file)

    print(f"\n{parquet_file}")
    print(f"Row Groups : {pf.num_row_groups}")

    total = 0

    for i in range(pf.num_row_groups):
        rg = pf.metadata.row_group(i)
        rows = rg.num_rows
        total += rows
        print(f"  RG {i+1:2d}: {rows:6d} rows")

    print(f"Total Rows : {total}")


# -------------------------------------------------------------

def main():

    parser = argparse.ArgumentParser()

    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument(
        "--parallelism",
        type=int,
        required=True,
        help="Desired number of Row Groups"
    )

    args = parser.parse_args()

    start = time.perf_counter()

    print("\nReading Parquet...")

    table = pq.read_table(args.input)

    schema = table.schema

    metadata = schema.metadata

    total_rows = table.num_rows

    print(f"Rows              : {total_rows}")
    print(f"Desired RGs       : {args.parallelism}")

    groups = min(args.parallelism, total_rows)

    base_rows = total_rows // groups
    remainder = total_rows % groups

    print(f"Rows / Row Group  : {base_rows}")
    print(f"Remaining         : {remainder}")

    writer = pq.ParquetWriter(
        args.output,
        schema.with_metadata(metadata)
    )

    offset = 0

    print("\nWriting Row Groups...")

    for i in range(groups):

        rows = base_rows

        if i < remainder:
            rows += 1

        subtable = table.slice(offset, rows)

        writer.write_table(subtable)

        print(
            f"  RG {i+1:2d}: "
            f"{rows:6d} rows"
        )

        offset += rows

    writer.close()

    end = time.perf_counter()

    print("\n--------------------------------------------------")

    print_row_groups(args.input)

    print_row_groups(args.output)

    print("\nExecution time : %.3f sec" % (end - start))


# -------------------------------------------------------------

if __name__ == "__main__":
    main()
