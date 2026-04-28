FROM espressif/idf:release-v6.0

ARG DEBIAN_FRONTEND=noninteractive

COPY run-docker.sh /tmp/
RUN /tmp/run-docker.sh

# Non-root user for use as a devcontainer remoteUser. The UID/GID below are
# placeholders; when used as a VS Code devcontainer, updateRemoteUserUID
# rewrites them to match the host user at container start so files in the
# workspace mount keep host ownership.
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID
RUN apt-get update \
    && apt-get install -y --no-install-recommends sudo \
    && if getent group $USER_GID >/dev/null; then \
           existing_group=$(getent group $USER_GID | cut -d: -f1); \
           if [ "$existing_group" != "$USERNAME" ]; then \
               groupmod --new-name $USERNAME "$existing_group"; \
           fi; \
       else \
           groupadd --gid $USER_GID $USERNAME; \
       fi \
    && if id -u $USER_UID >/dev/null 2>&1; then \
           existing_user=$(getent passwd $USER_UID | cut -d: -f1); \
           if [ "$existing_user" != "$USERNAME" ]; then \
               usermod --login $USERNAME --home /home/$USERNAME --move-home "$existing_user" 2>/dev/null \
                   || usermod --login $USERNAME "$existing_user"; \
           fi; \
       else \
           useradd --uid $USER_UID --gid $USER_GID -m -s /bin/bash $USERNAME; \
       fi \
    && usermod -aG dialout,plugdev $USERNAME \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME \
    && rm -rf /var/lib/apt/lists/*

# ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
# CMD [ "/bin/bash" ]
