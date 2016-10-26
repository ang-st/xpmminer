

From Ethos

install required packages
```
sudo apt install automake m4 libtool  protobuf-compiler  libprotobuf-dev libgmp-dev  libboost-all-dev
```

build dependencies

```
cd libsodium
./autogen.sh
./configure && make check
sudo make install
sudo ldconfig
cd .;
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

build fixed source


```
git clone https://github.com/ang-st/xpmminer
cd xpmminer
mkdir build
cd build 
cmake .. && make
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





