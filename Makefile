build:
	dotnet build
start:
	dotnet run --project src/Host/Host.csproj
nuget:
	nuget pack -NoDefaultExcludes -OutputDirectory nupkg
publish:
	dotnet publish -c Release
publish-image:
	docker build -t angelbus/tekton-webapi:1.0.0 -f Dockerfile . && 	\
	sudo docker push angelbus/tekton-webapi:1.0.0
publish-to-hub:
	dotnet publish -c Release -p:ContainerRegistry=docker.io -p:ContainerImageName=angelbus/tekton-webapi
ti: # terraform init
	cd terraform/environments/staging && terraform init
tp: # terraform plan
	cd terraform/environments/staging && terraform plan
ta: # terraform apply
	cd terraform/environments/staging && terraform apply
td: # terraform destroy
	cd terraform/environments/staging && terraform destroy
dcu: # docker-compose up : webapi + postgresql
	cd docker-compose/ && docker-compose -f docker-compose.postgresql.yml up -d
dcd: # docker-compose down : webapi + postgresql
	cd docker-compose/ && docker-compose -f docker-compose.postgresql.yml down
fds: # force rededeploy aws ecs service
	aws ecs update-service --force-new-deployment --service tekton-webapi --cluster angelbus
gw: # git docker workflow to push docker image to the repository based on the main branch
	@echo triggering github workflow to push docker image to container
	@echo ensure that you have the gh-cli installed and authenticated.
	gh workflow run dotnet-cicd -f push_to_docker=true