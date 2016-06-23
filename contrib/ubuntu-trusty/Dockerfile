FROM ubuntu:trusty

USER root
ENV HOME /root

ENV NODE_VER 6.2.2
ENV NVM_DIR=${HOME}/.nvm"

# this update is required to find apache2-dev, which is in updates.
RUN apt-get update
RUN apt-get install -y apache2 apache2-dev perl git-core curl libwww-perl dpkg-dev cdbs

# install nvm; use nvm to install node; set up a .profile to be sourced by your test task
RUN git clone https://github.com/creationix/nvm.git $HOME/.nvm
RUN echo '. ~/.nvm/nvm.sh' >> $HOME/.profile
RUN .  ~/.nvm/nvm.sh && nvm install ${NODE_VER} && nvm alias default ${NODE_VER}
RUN echo 'nvm install ${NODE_VER}' >> $HOME/.profile
