# nginx-php-cgi-service

安装服务命令 nginx-php-cgi-service install

卸载服务命令 nginx-php-cgi-service delete

ini配置文件命必须与exe文件名相同。

如nginx-php-cgi-service.exe,则配置文件为nginx-php-cgi-service.ini

下列是一个配置文件示例
```
[Nginx]
startCmd=D:\Program Files\NMP\Nginx\nginx.exe
stopCmd=D:\Program Files\NMP\Nginx\nginx.exe -s stop
reloadCmd=D:\Program Files\NMP\Nginx\nginx.exe -s reload
workPath=D:\Program Files\NMP\Nginx\

[PHP_FPM]
startCmd=D:\Program Files\NMP\php\php-7.3.33-nts-x64\php-cgi.exe
workPath=D:\Program Files\NMP\php\
StartServers=1
MinChildren=1
MaxChildren=4
LocalPort=9000
RestartDelay=0
```
CGI进程管理代码使用了[deemru/php-cgi-spawner](https://github.com/deemru/php-cgi-spawner "deemru/php-cgi-spawner")
