# econf
Automatically deploy configuration files.

## configuration
Within a configuration directory (usually a git repository), a file called
`econf` must be present. `econf` shall contain comments starting with `#`
and configuration directives.

symlink directory `source_directory` to `target_directory`

`dir source_directory target_directory`

symlink files in `source_directory` to `target_directory` as hidden files

`files source_directory target_directory`

### example

```
files:

config_repo/
        dunst/
            dunstrc
        zsh/
            zshrc
            zprofile
```

```
# symlink dir dunst to ~/.config
dir dunst ~/.config
# symlink files in zsh to ~
files zsh ~
```
