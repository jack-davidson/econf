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

### `sys` option
At the end of a configuration command, if you put `sys`, the hostname of your machine
will get appended to the source directory. This is useful for having different configs for
different machines. You can have a directory alsa-lt0 and alsa-dt0 where the hostnames are lt0
and dt0 respectively. If you put sys at the end of a directive, the directory with your hostname
postfix will be linked to the target directory.

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

```
files:

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
