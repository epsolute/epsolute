# DP-ORAM experiments

To install environment

```sh
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
cd ~
```
