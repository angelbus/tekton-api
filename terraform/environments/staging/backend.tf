terraform {
  backend "s3" {
    bucket         = "angel-backend"
    key            = "api/staging/terraform.tfstate"
    region         = "us-east-1"
    dynamodb_table = "angel-state-locks"
  }
}
