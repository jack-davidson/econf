# econf
Automatically deploy configuration files.

## configuration
Econf reads a configuration file called `econf` in your configuration repository.

### `dir`
`dir`: link a directory to another directory:

`dir directory another_directory`

### `files`
`files`: link files in directory to another directory as hidden files

`files directory another_directory`

### `@`
If @ is at the end of the second field in a config line, it will be replaced with a dash
and a hostname. This is helpful for using different config files for different machines.

example:

```
files xinit@ ~/
```
would expand to

```
files xinit-hostnameofmachine ~/
```

### `run`
Run a command:

`run command arg arg ...`

### comments
A comment can exist in its own line or at the end of a line. A line starting with `#` is a comment.
A line may have a trailing comment as well.

```
# line comment
example # trailing comment
```

### example

Directory structure:
```
$ ls
    config_repo/
        qutebrowser/
            ...
        fontconfig/
            ...
        nvim/
            ...
        zathura/
            ...
        dunst/
            ...
        spotifyd/
            ...
        spotify-tui/
            ...
        xinit-lt0/ # hostname postfix
            xinitrc
        xinit-dt0/ # hostname postfix
            xinitrc
        alsa-lt0/ # hostname postfix
            asoundrc
        alsa-dt0/ # hostname postfix
            asoundrc
        tmux/
            tmux.conf
        zsh/
            zshrc
            zsh_profile
```

econf config file
```
dir     qutebrowser    ~/.config
dir     fontconfig     ~/.config
dir     nvim           ~/.config
dir     zathura        ~/.config
dir     dunst          ~/.config
dir     spotifyd       ~/.config
dir     spotify-tui    ~/.config

# hostname postfix
files   xinit  ~/      sys
# hostname postfix
files   alsa   ~/      sys

files   tmux ~
files   zsh  ~

run sh -c 'curl -fLo "${XDG_DATA_HOME:-$HOME/.local/share}"/nvim/site/autoload/plug.vim --create-dirs https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim'
```
