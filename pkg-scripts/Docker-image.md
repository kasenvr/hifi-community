# Building the server image

## _This is the instruction of how to build the image that contains the domain-server and the assignment-clients (and the ice-server) using the Dockerfile_

---
## _How to build the image_

 - clone the repo or the folder named "vircadia_server_image" and jump into that directory
 - run the following command in your terminal (Ubuntu 20.04)
 - make sure you assign a name to the image by replacing image tag.

 ```
docker build -t <image tag> .
 ```

 ---

 ## _How to check the image is built successfully_

 - run the following command (after starting docker desktop if you have).

```
docker image ls
```

 - if the image is built properly, you should be able to find the image in the list.


