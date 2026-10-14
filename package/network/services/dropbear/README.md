# Service restart ownership

`/etc/init.d/dropbear restart` queues the existing stop/start operation through
procd. The command first registers the normal triggers with an additive service
update, preserving all current instances, then submits the restart event. This
also works when the service is stopped or its configured instances have changed.

The operation must belong to PID1: SSH sessions inherit their server's service
cgroup, and deleting that service terminates its descendants. A shell restart or
detached child in that cgroup can therefore die between stop and start. The queued
command survives removal of the service's trigger registration and starts the
service from current UCI configuration. Ordinary cgroup cleanup remains enabled.

A successful restart command acknowledges submission, not a listening server.
Callers that require completion must observe the new running instance and a fresh
SSH connection. The `restart_service` command is the procd trigger's implementation
entry point; interactive callers use `restart`. Existing reload and shutdown
behavior is unchanged.
