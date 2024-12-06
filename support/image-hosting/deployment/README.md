# Deployment

```shell
# Write vault pass into a text file:
% echo -n "your_vault_pass" > ./vault_pass.txt

# Write sudo password into a text file:
% echo -n "your_remote_sudo_pass" > ./sudo_pass.txt

# Run the playbook:
ansible-playbook  playbook.yml  --vault-password-file=./vault_pass.txt

# Run necessary tasks:
ansible-playbook  playbook.yml  --vault-password-file=./vault_pass.txt -e "install_deps=true" -e "build_image=true"
```

# Troubleshooting

After restarting Home Assistant, you may need to re-deploy the image server if the dashboard you want is not displayed
by default:

```shell
ansible-playbook  playbook.yml  --vault-password-file=./vault_pass.txt -e "install_deps=true" 
```