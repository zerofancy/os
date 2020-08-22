### 
# @Website: https://ntutn.top
 # @Date: 2019-11-18 20:25:52
 # @LastEditors: zero
 # @LastEditTime: 2019-11-18 20:53:15
 # @Description: 删除用户脚本
 # @FilePath: /os/useroperation/deluser.sh
 ###
echo "删除4*30个用户"

for i in {1..4}
do
	for j in {1..30}
	do
		str="class"$i"stu"$j
		echo "删除"$str
		sudo userdel -r $str
	done
done
