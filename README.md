
##From Ethos (for card not well supported by AMDGPU-PRO)

install required packages
```
sudo apt install automake m4 libtool  protobuf-compiler  libprotobuf-dev libgmp-dev  libboost-all-dev
```

build dependencies

```
git clone https://github.com/jedisct1/libsodium.git
cd libsodium
./autogen.sh
./configure && make check
sudo make install
sudo ldconfig
cd ..
git clone git://github.com/zeromq/libzmq.git
cd libzmq
./autogen.sh
./configure && make check
sudo make install
sudo ldconfig
cd ..
git clone git://github.com/zeromq/czmq.git
cd czmq
./autogen.sh
./configure && make check
sudo make install
sudo ldconfig
cd ..
```
BUILD

```
git clone https://github.com/ang-st/xpmminer
cd xpmminer
mkdir build
cd build 
cmake .. && make
cd ..
```


##From Ubuntu 16.04 Desktop

install AMDGPU-PRO

```
tar -Jxvf amdgpu-pro_16.30.3-315407.tar.xz
amdgpu-pro-driver/amdgpu-pro-install
sudo usermod -a -G video $LOGNAME 

```
install dependencies

```
apt-get install libprotobuf-dev/xenial libczmq-dev/xenial libgmp-dev cmake
apt-get install protobuf-compiler
apt-get install libboost-all-dev
apt install libsodium-dev

```

BUILD

```
git clone https://github.com/ang-st/xpmminer
cd xpmminer
mkdir build
cd build 
cmake .. -DOPENCL_LIBRARY=/usr/lib/x86_64-linux-gnu/amdgpu-pro/libOpenCL.so -DOPENCL_INCLUDE_DIRECTORY=/usr/src/amdgpu-pro-16.30.3-315407/amd/ 
make 
cd ..
```

create a config file

```
cat >>config.txt<<EOF
server = "coinsforall.io";
port = "6668";
#foo
name = "zooor";
address = "ztj7tziwZZbatY3krirjvDcw4cEQH8iUJrh5wHdx7iEsZHDm2G91FATpmytG6aFCt83aKtFtS4MeMCN9FUKytbNYo2aJShp";
platform = "amd";
EOF
#run
./build/zcashgpuminer
```


Have fun :)





