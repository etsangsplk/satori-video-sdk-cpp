.RECIPEPREFIX = >

.PHONY: all conan-create conan-create-in-docker conan-upload-from-docker conan-login conan-upload

CONAN_CREATE_ARGS ?= --build=outdated

DOCKER_TAG=satorivideo

all: conan-create-in-docker

conan-create:
> conan create satorivideo/master ${CONAN_CREATE_ARGS}

conan-create-in-docker:
> docker build -t ${DOCKER_TAG} .

conan-upload-from-docker:
> docker run --rm ${DOCKER_TAG} bash -c "CONAN_REMOTE=${CONAN_REMOTE} CONAN_SERVER=${CONAN_SERVER} CONAN_USER=${CONAN_USER} CONAN_PASSWORD=${CONAN_PASSWORD} make conan-login conan-upload"

## FIXME: had to duplicate it for now
conan-login:
> conan remote remove ${CONAN_REMOTE} ; conan remote add ${CONAN_REMOTE} ${CONAN_SERVER}
> conan user --remote ${CONAN_REMOTE} -p ${CONAN_PASSWORD} ${CONAN_USER}

## FIXME: had to duplicate it for now
conan-upload:
> conan upload --confirm --all --remote ${CONAN_REMOTE} '*@satorivideo/*'
> conan remove -r ${CONAN_REMOTE} -f --outdated '*@satorivideo/*'
