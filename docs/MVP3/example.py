def validate_s3_path(s3_key: str) -> dict:
    pattern = r"""
    ^
    (?P<jobType>[^/]+)/
    (?P<country>[^/]+)/
    (?P<purpose>[^/]+)/
    (?P<date>\d{8})/
    (?P<rest>.+)/
    (?P<filename>[^/]+\.parquet)
    $
    """

    match = re.match(pattern, s3_key, re.VERBOSE)
    if not match:
        raise ValidationError(f"Invalid S3 path: {s3_key}")

    components = match.groupdict()

    # Validate date
    datetime.strptime(components["date"], "%Y%m%d")

    # Process dynamic segments
    rest_parts = components["rest"].split("/")

    iteration = rest_parts[-1]
    if not iteration.isdigit():
        raise ValidationError("Invalid iteration")

    components["iteration"] = iteration
    components["dynamic_parts"] = rest_parts[:-1]

    return components
