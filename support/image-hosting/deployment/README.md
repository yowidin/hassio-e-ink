# Deployment

```shell
# Write vault pass into a text file:
% echo -n "your_vault_pass" > ./vault_pass.txt

# Write sudo password into a text file:
% echo -n "your_remote_sudo_pass" > ./sudo_pass.txt

# Run the playbook:
ansible-playbook  playbook.yml  --vault-password-file=./vault_pass.txt

# Run necessary tasks:
ansible-playbook  playbook.yml  --vault-password-file=./vault_pass.txt -e "install_deps=true" -e "build_image=true" -e "start_container=true"
```