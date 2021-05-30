# econf
Automatically deploy configuration files.

## configuration

symlink directory `source_directory` to `target_directory`

`dir source_directory target_directory`

symlink files in `source_directory` to `target_directory` as hidden files

`files source_directory target_directory`

### example
```
# symlink dir dunst to ~/.config
dir dunst ~/.config
# symlink files in zsh to ~
files zsh ~
```
