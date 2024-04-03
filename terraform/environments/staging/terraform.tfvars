// AWS Configuration
aws_region   = "us-east-1"
aws_region_a = "us-east-1a"
aws_region_b = "us-east-1b"

// Default Project Tags
environment  = "staging"
owner        = "Angel Bustamante"
project_name = "angel-tekton-webapi"

// RDS PostgreSQL Configuration
pg_password = "posgresqladmin"
pg_username = "posgresqladmin"
db_name     = "fshdb"

ecs_cluster_name = "angelbus"

api_container_cpu    = 512
api_container_memory = 1024
api_image_name       = "angelbus/tekton-webapi:latest"
api_service_name     = "tekton-webapi"

enable_health_check = true
