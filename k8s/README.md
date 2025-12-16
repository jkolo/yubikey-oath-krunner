# Kubernetes Manifests

Kubernetes deployment configuration for KRunner YubiKey OATH Plugin CI/CD testing.

## Overview

This directory contains Kubernetes manifests for running builds and tests in a containerized, isolated environment. These manifests are used both for local testing (kind/minikube) and can be adapted for production CI/CD runners.

## Files

| File | Purpose | Resources |
|------|---------|-----------|
| `namespace.yaml` | Isolated namespace for CI/CD | 1 Namespace |
| `configmap.yaml` | Build/test configuration | 3 ConfigMaps |
| `secret.yaml.example` | Template for credentials | 3 Secrets (template) |
| `pvc.yaml` | Persistent storage for build cache | 2 PVCs (7Gi total) |
| `build-job.yaml` | Kubernetes Job for building project | 1 Job |
| `test-job.yaml` | Kubernetes Job for running tests | 1 Job (3 containers) |

## Quick Start

### Prerequisites

**Option 1: kind (Kubernetes in Docker)**
```bash
# Install kind
curl -Lo ./kind https://kind.sigs.k8s.io/dl/v0.20.0/kind-linux-amd64
chmod +x ./kind
sudo mv ./kind /usr/local/bin/kind

# Or via package manager
sudo pacman -S kind  # Arch Linux
```

**Option 2: minikube**
```bash
# Install minikube
curl -LO https://storage.googleapis.com/minikube/releases/latest/minikube-linux-amd64
sudo install minikube-linux-amd64 /usr/local/bin/minikube

# Or via package manager
sudo pacman -S minikube  # Arch Linux
```

**kubectl (required for both)**
```bash
sudo pacman -S kubectl
```

### Deploy with Helper Script

```bash
# Create cluster (kind)
kind create cluster --name krunner-test

# Run complete pipeline
./scripts/k8s-deploy.sh full

# View logs
./scripts/k8s-deploy.sh logs test-job

# Cleanup
./scripts/k8s-deploy.sh cleanup
kind delete cluster --name krunner-test
```

### Manual Deployment

**1. Create cluster:**
```bash
kind create cluster --name krunner-test
# Or: minikube start --driver=docker
```

**2. Configure registry credentials:**
```bash
# Copy secret template
cp k8s/secret.yaml.example k8s/secret.yaml

# Edit with your credentials
vim k8s/secret.yaml

# Or create via kubectl
kubectl create secret docker-registry registry-credentials \
  --docker-server=registry.kolosowscy.pl \
  --docker-username=YOUR_USERNAME \
  --docker-password=YOUR_PASSWORD \
  -n krunner-yubikey-ci
```

**3. Deploy resources:**
```bash
kubectl apply -f k8s/namespace.yaml
kubectl apply -f k8s/configmap.yaml
kubectl apply -f k8s/pvc.yaml
kubectl apply -f k8s/secret.yaml  # If created manually
```

**4. Run build:**
```bash
kubectl apply -f k8s/build-job.yaml
kubectl wait --for=condition=complete --timeout=15m job/build-job -n krunner-yubikey-ci
```

**5. Run tests:**
```bash
kubectl apply -f k8s/test-job.yaml
kubectl wait --for=condition=complete --timeout=10m job/test-job -n krunner-yubikey-ci
```

**6. View results:**
```bash
# Get pod name
POD=$(kubectl get pods -n krunner-yubikey-ci -l app.kubernetes.io/component=test --sort-by=.metadata.creationTimestamp -o jsonpath='{.items[-1].metadata.name}')

# View test logs
kubectl logs -n krunner-yubikey-ci $POD -c test-runner

# Copy coverage report
kubectl cp krunner-yubikey-ci/$POD:/artifacts/coverage-report ./coverage-report -c test-runner
```

**7. Cleanup:**
```bash
kubectl delete namespace krunner-yubikey-ci
kind delete cluster --name krunner-test
# Or: minikube delete
```

## Resource Details

### Namespace

**File:** `namespace.yaml`

Creates isolated namespace `krunner-yubikey-ci` with labels:
- `app.kubernetes.io/name: krunner-yubikey-oath`
- `app.kubernetes.io/component: ci-cd`
- `app.kubernetes.io/part-of: krunner-yubikey-oath`

### ConfigMaps

**File:** `configmap.yaml`

**build-config:**
- `CMAKE_PRESET=clang-release`
- `CMAKE_BUILD_TYPE=Release`
- `ENABLE_COVERAGE=ON`
- `CCACHE_MAXSIZE=2G`

**test-config:**
- `QT_QPA_PLATFORM=offscreen` - Headless Qt
- `DISPLAY=:99` - Xvfb display
- `DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-session`
- `CTEST_PARALLEL_LEVEL=4` - Parallel test execution

**dbus-config:**
- D-Bus session configuration file
- Mounted at `/etc/dbus-1/session.conf`

### Secrets

**File:** `secret.yaml.example` (template only)

**Required secrets:**

1. **registry-credentials** (`kubernetes.io/dockerconfigjson`)
   - Registry: `registry.kolosowscy.pl`
   - Used by: `imagePullSecrets`

2. **github-token** (`Opaque`)
   - Key: `token`
   - Value: GitHub Personal Access Token (base64)
   - Used by: Deploy jobs (GitHub release)

3. **aur-ssh-key** (`Opaque`)
   - Keys: `ssh-privatekey`, `ssh-publickey`
   - Values: SSH key pair for AUR (base64)
   - StringData: `git-email`, `git-name`
   - Used by: Deploy jobs (AUR update)

**Creating secrets:**

```bash
# Registry credentials
kubectl create secret docker-registry registry-credentials \
  --docker-server=registry.kolosowscy.pl \
  --docker-username=USER \
  --docker-password=PASS \
  -n krunner-yubikey-ci

# GitHub token
kubectl create secret generic github-token \
  --from-literal=token=ghp_YOUR_TOKEN_HERE \
  -n krunner-yubikey-ci

# AUR SSH key
kubectl create secret generic aur-ssh-key \
  --from-file=ssh-privatekey=$HOME/.ssh/aur_ed25519 \
  --from-file=ssh-publickey=$HOME/.ssh/aur_ed25519.pub \
  --from-literal=git-email=ci@kolosowscy.pl \
  --from-literal=git-name="GitLab CI" \
  -n krunner-yubikey-ci
```

### Persistent Volume Claims

**File:** `pvc.yaml`

**build-cache-pvc:**
- Size: 5Gi
- Access: ReadWriteOnce
- Purpose: ccache + CMake build artifacts
- Speeds up incremental builds by ~60%

**test-artifacts-pvc:**
- Size: 2Gi
- Access: ReadWriteOnce
- Purpose: Coverage reports, JUnit XML
- Persists test results across job runs

**Storage class:**
- Default: `standard`
- Adjust for your cluster:
  - kind/minikube: `standard` (hostPath)
  - AWS: `gp2`, `gp3`
  - GCP: `pd-standard`, `pd-ssd`
  - Azure: `managed-premium`

### Build Job

**File:** `build-job.yaml`

**Architecture:**
```
┌─────────────────────────────┐
│  InitContainer: git-clone  │
│  Clone source if needed     │
└─────────────────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  Container: builder         │
│  - CMake configure          │
│  - CMake build -j$(nproc)   │
│  - Artifacts to PVC         │
└─────────────────────────────┘
```

**Resources:**
- Request: 2 CPUs, 4Gi RAM
- Limit: 4 CPUs, 8Gi RAM

**Volumes:**
- `workspace` (emptyDir) - Source code
- `build-cache` (PVC) - ccache storage

**Timeout:** 15 minutes (via `kubectl wait`)

**TTL:** 24 hours (job auto-deleted after completion)

### Test Job

**File:** `test-job.yaml`

**Architecture:**
```
┌────────────────────────────────────────────┐
│  InitContainer: copy-build-artifacts       │
│  Wait for build completion                 │
└────────────────────────────────────────────┘
           │
           ▼
┌────────────────────────────────────────────┐
│  Pod with 3 containers (shared namespace)  │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │  Sidecar: dbus-daemon                │ │
│  │  Runs D-Bus session bus              │ │
│  │  Socket: /tmp/dbus-session           │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │  Sidecar: xvfb                       │ │
│  │  Virtual X11 server                  │ │
│  │  Display: :99                        │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │  Main: test-runner                   │ │
│  │  - Wait for sidecars ready           │ │
│  │  - Run CTest with parallel=4         │ │
│  │  - Generate coverage report          │ │
│  │  - Copy artifacts to PVC             │ │
│  └──────────────────────────────────────┘ │
│                                            │
└────────────────────────────────────────────┘
```

**Main container resources:**
- Request: 2 CPUs, 4Gi RAM
- Limit: 4 CPUs, 8Gi RAM

**Sidecar resources (each):**
- Request: 100m CPU, 128Mi RAM
- Limit: 200-500m CPU, 256-512Mi RAM

**Volumes:**
- `workspace` (emptyDir) - Source code
- `build-cache` (PVC) - Build artifacts
- `test-artifacts` (PVC) - Coverage reports, JUnit XML
- `dbus-socket` (emptyDir) - D-Bus session socket
- `dbus-config` (ConfigMap) - D-Bus configuration
- `x11-socket` (emptyDir) - X11 socket

**Process namespace sharing:**
- `shareProcessNamespace: true`
- Allows sidecars to coordinate lifecycle

**Timeout:** 10 minutes (via `kubectl wait`)

**TTL:** 24 hours

## Usage Patterns

### Local Development Testing

**Test build in Kubernetes (mirrors CI):**
```bash
kind create cluster --name test
./scripts/k8s-deploy.sh build
kubectl logs -f job/build-job -n krunner-yubikey-ci
./scripts/k8s-deploy.sh cleanup
kind delete cluster --name test
```

### Debugging Failed Tests

**Interactive shell in test pod:**
```bash
# Start test job
kubectl apply -f k8s/test-job.yaml

# Wait for pod to start
kubectl wait --for=condition=Ready pod -l app.kubernetes.io/component=test -n krunner-yubikey-ci

# Get pod name
POD=$(kubectl get pods -n krunner-yubikey-ci -l app.kubernetes.io/component=test -o jsonpath='{.items[0].metadata.name}')

# Shell into test container
kubectl exec -it $POD -n krunner-yubikey-ci -c test-runner -- bash

# Inside pod:
cd /workspace/build-clang-release
ctest --output-on-failure --verbose
```

### Viewing Sidecar Logs

**D-Bus daemon:**
```bash
POD=$(kubectl get pods -n krunner-yubikey-ci -l app.kubernetes.io/component=test -o jsonpath='{.items[0].metadata.name}')
kubectl logs $POD -n krunner-yubikey-ci -c dbus-daemon
```

**Xvfb:**
```bash
kubectl logs $POD -n krunner-yubikey-ci -c xvfb
```

### Extracting Artifacts

**Coverage report:**
```bash
POD=$(kubectl get pods -n krunner-yubikey-ci -l app.kubernetes.io/component=test --sort-by=.metadata.creationTimestamp -o jsonpath='{.items[-1].metadata.name}')

kubectl cp krunner-yubikey-ci/$POD:/artifacts/coverage-report ./coverage-report -c test-runner

# View locally
xdg-open coverage-report/index.html
```

**Test results (JUnit XML):**
```bash
kubectl cp krunner-yubikey-ci/$POD:/artifacts/ ./k8s-test-results -c test-runner
```

## GitLab CI/CD Integration

These manifests can be used with GitLab Kubernetes executor:

**`.gitlab-ci.yml` integration:**
```yaml
test:k8s:
  stage: test
  tags:
    - kubernetes
  script:
    - kubectl apply -f k8s/namespace.yaml
    - kubectl apply -f k8s/configmap.yaml
    - kubectl apply -f k8s/build-job.yaml
    - kubectl wait --for=condition=complete job/build-job -n krunner-yubikey-ci
    - kubectl apply -f k8s/test-job.yaml
    - kubectl wait --for=condition=complete job/test-job -n krunner-yubikey-ci
  after_script:
    - kubectl delete namespace krunner-yubikey-ci
```

**Note:** Current pipeline uses Docker executor with podman. Kubernetes executor is optional.

## Customization

### Adjusting Resources

**More CPU for faster builds:**
```yaml
# In build-job.yaml
resources:
  requests:
    cpu: "4"
    memory: "8Gi"
  limits:
    cpu: "8"
    memory: "16Gi"
```

### Storage Class

**For faster local testing (hostPath):**
```yaml
# In pvc.yaml
storageClassName: local-path  # kind default
```

**For production (cloud):**
```yaml
storageClassName: gp3  # AWS EBS
# or
storageClassName: pd-ssd  # Google Cloud
```

### Parallel Test Execution

**Run tests faster with more cores:**
```yaml
# In configmap.yaml (test-config)
data:
  CTEST_PARALLEL_LEVEL: "8"  # Use 8 cores instead of 4
```

## Troubleshooting

**Issue: PVC remains in Pending state**
```bash
# Check storage class availability
kubectl get storageclass

# For kind, ensure local-path provisioner is installed
kubectl apply -f https://raw.githubusercontent.com/rancher/local-path-provisioner/master/deploy/local-path-storage.yaml
```

**Issue: ImagePullBackOff**
```bash
# Check secret exists
kubectl get secret registry-credentials -n krunner-yubikey-ci

# Verify secret content
kubectl get secret registry-credentials -n krunner-yubikey-ci -o yaml

# Test login manually
podman login registry.kolosowscy.pl
```

**Issue: D-Bus socket not found**
```bash
# Check dbus-daemon sidecar is running
kubectl logs $POD -n krunner-yubikey-ci -c dbus-daemon

# Verify socket exists
kubectl exec $POD -n krunner-yubikey-ci -c test-runner -- ls -la /tmp/dbus-session
```

**Issue: Xvfb connection refused**
```bash
# Check Xvfb sidecar logs
kubectl logs $POD -n krunner-yubikey-ci -c xvfb

# Verify display variable
kubectl exec $POD -n krunner-yubikey-ci -c test-runner -- env | grep DISPLAY
```

**Issue: Tests timeout**
```bash
# Increase timeout
kubectl wait --for=condition=complete --timeout=20m job/test-job -n krunner-yubikey-ci

# Or reduce parallel level in configmap.yaml
CTEST_PARALLEL_LEVEL: "2"
```

## Related Documentation

- [Container Development](../docs/CONTAINERS.md) - Local Podman/Docker usage
- [CI/CD Pipeline](../docs/CI_CD.md) - GitLab CI/CD configuration
- [Main README](../README.md) - Project overview

## Support

For Kubernetes-specific issues:
1. Check pod status: `kubectl get pods -n krunner-yubikey-ci`
2. View pod events: `kubectl describe pod $POD -n krunner-yubikey-ci`
3. Check job status: `kubectl get jobs -n krunner-yubikey-ci`
4. View logs: `kubectl logs $POD -n krunner-yubikey-ci -c <container-name>`
5. Report bugs: https://github.com/jkolo/yubikey-oath-krunner/issues
