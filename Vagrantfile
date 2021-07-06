# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|
  config.vm.box = "ubuntu/focal64"

  config.vm.provider "virtualbox" do |vb|
    vb.name = "AFL Snapshot LKM"
    vb.customize ["modifyvm", :id, "--uart1", "0x3F8", "4"]
    vb.customize ["modifyvm", :id, "--uartmode1", "tcpserver", "2023"]
  end

  config.vm.provision "shell", inline: <<-'SHELL'
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y build-essential flex bison

    sed -i \
        's/\(GRUB_CMDLINE_LINUX\)="\(.*\)"/\1="kgdboc=ttyS0,115200 nokaslr no_hash_pointers"/' \
        /etc/default/grub
    update-grub
  SHELL
end
