echo "------------------BUILDING------------------"
./build.sh
echo "------------------END-----------------------"
fastboot flash boot boot-uefi.img
fastboot reboot
