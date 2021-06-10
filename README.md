# econf
Automatically deploy configuration files.

## usage
Receive help for econf with
`econf -h`

## configuration
Econf reads a configuration file called `econf` in your configuration repository.

### `noconfirm`
don't ask user for input, same as -f option

### `dir`
`dir`: link a directory to another directory:

`dir directory another_directory`

### `install`
`install` executes an installation script located in `install/`

example:

`install vim-plug`
...
```
ls install/
    vim-plug
```
vim-plug gets executed

### `files`
`files`: link files in directory to another directory as hidden files

`files directory another_directory`

### `:host`
:host appended at the end of a token will expand to :$(hostname) allowing different files to
be used for different machines.

example:

```
files xinit:host ~/
```
would expand to

```
files xinit:hostnameofmachine ~/
```

### `sh`
Run a command:

`sh command arg arg ...`

### comments
A line starting with `#` is a comment.

```
# comment
```
