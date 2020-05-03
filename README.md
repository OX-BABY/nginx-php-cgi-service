# nginx-php-cgi-service

安装服务命令 nginx-php-cgi-service install
卸载服务命令 nginx-php-cgi-service delete
ini配置文件命必须与exe文件名相同。
如nginx-php-cgi-service.exe,则配置文件为nginx-php-cgi-service.ini
下列是一个配置文件示例
[Nginx]
startCmd=D:\nginx\nginx.exe
stopCmd=D:\nginx\nginx.exe -s stop
workPath=D:\nginx\

[PHP_FPM]
startCmd=D:\php\php-7.3.9-nts\php-cgi.exe
StartServers=2 ;服务启动时创建CGI进程个数
MaxChildren=4 ;最大CGI进程个数
LocalPort=9000 ;监听端口
RestartDelay=0
