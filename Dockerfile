FROM ubuntu:latest

RUN apt-get update
RUN apt-get install -y build-essential git m4 scons zlib1g zlib1g-dev libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev python-dev python automake

CMD ["sh", "-c", "${GEM5_LOC}/build/X86/gem5.opt ${GEM5_LOC}/configs/example/fs.py"]
