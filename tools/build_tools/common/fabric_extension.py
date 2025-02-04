"""
a clone of https://github.com/fabric/patchwork
with small improvements: added the support of bi-directional rsync
"""

from invoke.vendor import six


def rsync(c,
          local_path,
          remote_path,
          exclude=None,
          delete=False,
          strict_host_keys=True,
          rsync_opts="",
          ssh_opts="",
          mode=None,
          remote_to_local=False):
    """
    Convenient wrapper around your friendly local ``rsync``.

    Specifically, it calls your local ``rsync`` program via a subprocess, and
    fills in its arguments with Fabric's current target host/user/port. It
    provides Python level keyword arguments for some common rsync options, and
    allows you to specify custom options via a string if required (see below.)

    For details on how ``rsync`` works, please see its manpage. ``rsync`` must
    be installed on both the invoking system and the target in order for this
    function to work correctly.

    .. note::
        This function transparently honors the given
        `~fabric.connection.Connection`'s connection parameters such as port
        number and SSH key path.

    .. note::
        For reference, the approximate ``rsync`` command-line call that is
        constructed by this function is the following::

            rsync [--delete] [--exclude exclude[0][, --exclude[1][, ...]]] \\
                -pthrvz [rsync_opts] <source> <host_string>:<target>

    :param c:
        `~fabric.connection.Connection` object upon which to operate.
    :param mode:
        mode of the file
    :param str local_path:
        The local path to copy from. Actually a string passed verbatim to
        ``rsync``, and thus may be a single directory (``"my_directory"``) or
        multiple directories (``"dir1 dir2"``). See the ``rsync`` documentation
        for details.
    :param str remote_path:
        The path to sync with on the remote end. Due to how ``rsync`` is
        implemented, the exact behavior depends on the value of ``source``:

        - If ``source`` ends with a trailing slash, the files will be dropped
          inside of ``target``. E.g. ``rsync(c, "foldername/",
          "/home/username/project")`` will drop the contents of ``foldername``
          inside of ``/home/username/project``.
        - If ``source`` does **not** end with a trailing slash, ``target`` is
          effectively the "parent" directory, and a new directory named after
          ``source`` will be created inside of it. So ``rsync(c, "foldername",
          "/home/username")`` would create a new directory
          ``/home/username/foldername`` (if needed) and place the files there.

    :param exclude:
        Optional, may be a single string or an iterable of strings, and is
        used to pass one or more ``--exclude`` options to ``rsync``.
    :param bool delete:
        A boolean controlling whether ``rsync``'s ``--delete`` option is used.
        If True, instructs ``rsync`` to remove remote files that no longer
        exist locally. Defaults to False.
    :param bool strict_host_keys:
        Boolean determining whether to enable/disable the SSH-level option
        ``StrictHostKeyChecking`` (useful for frequently-changing hosts such as
        virtual machines or cloud instances.) Defaults to True.
    :param str rsync_opts:
        An optional, arbitrary string which you may use to pass custom
        arguments or options to ``rsync``.
    :param str ssh_opts:
        Like ``rsync_opts`` but specifically for the SSH options string
        (rsync's ``--rsh`` flag.)
    :param bool remote_to_local:
        boolean flag that indicate whether you want to sync from
        remote to local. By default it is sync from local to remote.

    Args:
        mode ():
    """
    # Turn single-string exclude into a one-item list for consistency
    if isinstance(exclude, six.string_types):
        exclude = [exclude]
    if exclude is None:
        exclude = ()
    # Create --exclude options from exclude list
    exclude_opts = ' --exclude "{}"' * len(exclude)
    # Double-backslash-escape
    exclusions = tuple([str(s).replace('"', '\\\\"') for s in exclude])
    # Honor SSH key(s)
    key_string = ""
    # TODO: seems plausible we need to look in multiple places if there's too
    # much deferred evaluation going on in how we eg source SSH config files
    # and so forth, re: connect_kwargs
    # TODO: we could get VERY fancy here by eg generating a tempfile from any
    # in-memory-only keys...but that's also arguably a security risk, so...
    keys = c.connect_kwargs.get("key_filename", [])
    # TODO: would definitely be nice for Connection/FabricConfig to expose an
    # always-a-list, always-up-to-date-from-all-sources attribute to save us
    # from having to do this sort of thing. (may want to wait for Paramiko auth
    # overhaul tho!)
    if isinstance(keys, six.string_types):
        keys = [keys]
    if keys:
        key_string = "-i " + " -i ".join(keys)
    # Get base cxn params
    user, host, port = c.user, c.host, c.port
    port_string = "-p {}".format(port)
    # Remote shell (SSH) options
    rsh_string = ""
    # Strict host key checking
    disable_keys = "-o StrictHostKeyChecking=no"
    if not strict_host_keys and disable_keys not in ssh_opts:
        ssh_opts += " {}".format(disable_keys)
    rsh_parts = [key_string, port_string, ssh_opts]
    if any(rsh_parts):
        rsh_string = "--rsh='ssh {}'".format(" ".join(rsh_parts))
    # Set up options part of string

    chmod_option = f"--chmod {mode}" if mode else ""

    options_map = {
        "delete": "--delete" if delete else "",
        "exclude": exclude_opts.format(*exclusions),
        "rsh": rsh_string,
        "extra": rsync_opts,
        "mode": chmod_option,
    }
    options = "{delete}{exclude} -pthrvz {extra} {mode} {rsh}".format(**options_map)
    # Create and run final command string
    # TODO: richer host object exposing stuff like .address_is_ipv6 or whatever
    if host.count(":") > 1:
        # Square brackets are mandatory for IPv6 rsync address,
        # even if port number is not specified
        if remote_to_local:
            cmd = "rsync {options} '[{user}@{host}]:{target}' '{source}'"
        else:
            cmd = "rsync {options} {source} [{user}@{host}]:{target}"
    else:
        if remote_to_local:
            cmd = "rsync {options} '{user}@{host}:{target}' '{source}'"
        else:
            cmd = "rsync {options} '{source}' '{user}@{host}:{target}'"

    cmd = cmd.format(options=options, source=local_path, user=user, host=host, target=remote_path)

    return c.local(cmd)


def check_if_exists_remote(remote_path, remote_connection, is_dir=False):
    """
    Check if dir/file exists on remote path
    Based of a stackoverflow solution:
    https://stackoverflow.com/questions/12845206/check-if-file-exists-on-remote-host-with-ssh/18290318#18290318
    :param is_dir: Bool; whether the remote_path is expected to be a directory
    :param remote_path: Path to the remote dir/file to check`
    :param remote_connection: `FabricWrapper` instance
    :return: Bool; whether the dir/file exists
    """
    file_found_msg = "OK"
    file_type_flag = 'd' if is_dir else 'f'
    check_if_file_exists_command = f'[ -{file_type_flag} {remote_path} ] && echo {file_found_msg} || echo "Failed"'
    check_if_file_exists, _, _ = remote_connection.run_command(check_if_file_exists_command)

    return check_if_file_exists.strip() == file_found_msg


def list_dir(remote_path, remote_connection, suffix=None):
    list_dir_command = f"ls {remote_path}"
    if suffix:
        list_dir_command += f" | grep .{suffix}"

    list_dir_results, _, _ = remote_connection.run_command(list_dir_command)
    list_dir_results = list_dir_results.strip().split()

    return list_dir_results
