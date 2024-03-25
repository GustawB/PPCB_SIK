# coding: utf-8

Vagrant.configure("2") do |config|
  config.vm.define "ppcbs" do |ppcbs|
    ppcbs.vm.network "private_network", ip: "192.168.42.10", virtualbox__intnet: "siknet", auto_config: true
    ppcbs.vm.provider "virtualbox" do |v|
      v.name = "ppcbs"
    end
  end

  config.vm.define "ppcbc" do |ppcbc|
    ppcbc.vm.network "private_network", ip: "192.168.42.11", virtualbox__intnet: "siknet", auto_config: true
    ppcbc.vm.provider "virtualbox" do |v|
      v.name = "ppcbc"
    end
  end

  config.vm.provider "virtualbox" do |v|
    v.gui = false
    v.memory = 1024
    v.linked_clone = true
  end

  config.vm.box = "mimuw/sikvm"
end
