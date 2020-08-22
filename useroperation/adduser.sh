### 
# @Website: https://ntutn.top
 # @Date: 2019-11-18 20:25:08
 # @LastEditors: zero
 # @LastEditTime: 2019-11-18 20:25:42
 # @Description: 脚本自动创建用户
 # @FilePath: /os/useroperation/adduser.sh
 ###

echo "创建4*30个用户"

for i in {1..4}
do
	for j in {1..30}
	do
		str="class"$i"stu"$j
		echo "创建"$str
		sudo useradd $str
	done
done
