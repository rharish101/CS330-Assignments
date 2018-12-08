# Operating Systems (CS330A) Assignments

This repository contains all of my assignment solutions for the Operating Systems course at IIT Kanpur for the odd semester of 2018-19.
This course was done under [Prof. Debadatta Mishra](https://www.cse.iitk.ac.in/users/deba/).
The course webpage is hosted [here](https://www.cse.iitk.ac.in/users/deba/cs330/) (as of 2018-12-08).

The first 3 assignments consist of development on an OS made using the [gem5](http://gem5.org/Main_Page) architectural simulator.
This OS will be referred to as gemOS.
Instructions for setting up gemOS is given in this [pdf](./gemos-howto.pdf).

## Table of Contents
* [Assignment 1](./gemOS-a1): Virtual memory and paging for gemOS.
* [Assignment 2](./gemOS-a2): System call implementations for gemOS.
* [Assignment 3](./gemOS-a3): Signal handling, sleeping and process scheduling for gemOS.
* [Assignment 4](./assignment-4): A FUSE filesystem acting as an object-store i.e. only files allowed - no directories at all, except the root directory.

## Running gem5 on a docker
I could not build gem5 on my system (Arch Linux), so I created a Dockerfile for building and running gemOS.

### Instructions
* Building the gemOS image:
  ```
  docker build -t gem5 .
  ```

* Starting a gemOS container:
  ```
  docker run -dit --name gemos --rm=true -p 1234:3456 -v /mnt/Data/gem5:/root/gem5 -e M5_PATH=\"/root/gem5/gemos\" -e GEM5_LOC=\"/root/gem5\" gem5
  ```
  Here, `/mnt/Data/gem5` is the path to where I've cloned the gem5 repository on my system.
  Replace it with the path to your cloned gem5 repository.

* Stopping the gemOS container:
  ```
  docker kill gemos
  ```

* Building gem5 on the gemOS container:
  ```
  docker exec -t /bin/bash gemos
  ```
  Then, once inside the gemOS container, go to `/root/gem5` and follow the instructions in the [howto](./gemos-howto.pdf) pdf to build gem5.

* Opening the gemOS shell on the gemOS container:
  ```
  telnet localhost 1234
  ```
