"""Module docstring."""
import logging
import subprocess
import time
_SENTINEL = object()

class ServoDriver:
    """Wrapper for servod API and dut-control execution."""

    def __init__(self, port='9999', dut_ip='', dry_run=False):
        self.port = port
        self.dut_ip = dut_ip
        self.dry_run = dry_run
        self._logger = logging.getLogger(self.__class__.__name__)
        self._restore_states = {}

    def _run_cmd(self, args):
        if self.dry_run:
            self._logger.info('DRY RUN: %s', ' '.join(args))
            return 'dry_run_output'
        try:
            result = subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            self._logger.error(f"dut-control command failed: {' '.join(args)}\nError: {e.stderr}")
            raise

    def get(self, control, default=_SENTINEL):
        """Docstring."""
        cmd = ['dut-control', '--', '-p', str(self.port)]
        if self.dut_ip:
            cmd.extend(['--host', self.dut_ip])
        cmd.append(control)
        try:
            out = self._run_cmd(cmd)
            if self.dry_run:
                return 'unknown'
            if ':' in out:
                return out.split(':', 1)[1].strip()
            return out
        except Exception:
            if default is not _SENTINEL:
                return default
            raise

    def set(self, control, value):
        """Docstring."""
        cmd = ['dut-control', '--', '-p', str(self.port)]
        if self.dut_ip:
            cmd.extend(['--host', self.dut_ip])
        cmd.append(f'{control}:{value}')
        self._run_cmd(cmd)

    def has_control(self, control):
        """Docstring."""
        return 'ERR' not in self.get(control, 'ERR')

    def is_ccd(self):
        """Docstring."""
        return 'ccd' in self.get('servo_type', '')

    def save_add(self, control, default_val=''):
        """Docstring."""
        if control not in self._restore_states:
            try:
                current = self.get(control)
                self._restore_states[control] = current
            except Exception:
                self._restore_states[control] = default_val

    def restore_all(self):
        """Docstring."""
        for control, val in reversed(list(self._restore_states.items())):
            if val:
                self._logger.info('Restoring %s:%s', control, val)
                try:
                    self.set(control, val)
                except Exception:
                    pass
        self._restore_states.clear()