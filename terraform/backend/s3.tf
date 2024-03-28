resource "aws_s3_bucket" "s3_bucket" {
  bucket = "angel-backend"
  tags = {
    Name = "angel-backend"
  }
  lifecycle {
    prevent_destroy = true
  }
}
