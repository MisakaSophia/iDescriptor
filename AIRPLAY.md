On macOS AirPlay should work out of the box.

On Windows you need to have Bonjour service installed and running, dependency check widget can do that for you within the app.

But on Linux you may need to open some ports in your firewall for it to work properly.

Special thanks to [@medaminezghal](https://github.com/medaminezghal) for figuring this out and sharing the information with us.

AirPlay requires these ports to be open on Linux

```shell
sudo firewall-cmd --add-port=7000/tcp --permanent
sudo firewall-cmd --add-port=7001/tcp --permanent
sudo firewall-cmd --add-port=7100/tcp --permanent
sudo firewall-cmd --add-port=6000/udp --permanent
sudo firewall-cmd --add-port=6001/udp --permanent
sudo firewall-cmd --add-port=7011/udp --permanent
sudo firewall-cmd --reload
```

If you want to use ufw instead of firewalld, you can run these commands:

```shell
sudo ufw allow 7000/tcp
sudo ufw allow 7001/tcp
sudo ufw allow 7100/tcp
sudo ufw allow 6000/udp
sudo ufw allow 6001/udp
sudo ufw allow 7011/udp
sudo ufw reload
```

I don't suggest disabling the `Use legacy ports` option in settings, if you have it disabled then ports might be different
