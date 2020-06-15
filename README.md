# DP-ORAM experiments

To install environment

```sh
byobu-enable
byobu

sudo apt update
sudo apt upgrade -y
sudo apt install -y git vim g++ make cmake libboost-all-dev libssl-dev

cd /tmp/
git clone https://github.com/redis/hiredis.git
cd hiredis
make
sudo make install
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
mkdir compile
cd compile
cmake -DCMAKE_BUILD_TYPE=Release -DREDIS_PLUS_PLUS_CXX_STANDARD=17 ..
make
sudo make install
git clone https://github.com/rpclib/rpclib.git
cd rpclib
mkdir build
cd build
cmake ..
cmake --build .
sudo make install
cd ~

ssh-keygen
cat ~/.ssh/id_rsa.pub

git clone git@git.dbogatov.org:bu/dp-oram/dp-oram.git
git clone git@git.dbogatov.org:bu/dp-oram/path-oram.git
git clone git@git.dbogatov.org:bu/dp-oram/b-plus-tree.git
git clone git@git.dbogatov.org:bu/dp-oram/experiments-scripts.git

cd ~/path-oram/path-oram
make clean && make shared CPPFLAGS="-DUSE_AEROSPIKE=false" -j8

cd ~/b-plus-tree/b-plus-tree/
make clean && make shared -j8

cd ~/dp-oram/dp-oram/
make clean copy-libs-dev && sudo make ldconfig && make main redis server -j8
```
