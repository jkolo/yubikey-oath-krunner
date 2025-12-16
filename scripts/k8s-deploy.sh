#!/bin/bash
# Deploy KRunner YubiKey OATH tests to Kubernetes (kind/minikube)
# Usage: ./scripts/k8s-deploy.sh [create|test|cleanup|logs]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
K8S_DIR="$PROJECT_ROOT/k8s"

cd "$PROJECT_ROOT"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse command
COMMAND="${1:-create}"

# Namespace
NAMESPACE="krunner-yubikey-ci"

echo -e "${BLUE}======================================"
echo "Kubernetes Deployment for CI/CD Tests"
echo "======================================${NC}"
echo "Command: $COMMAND"
echo "Namespace: $NAMESPACE"
echo ""

# Function: Create namespace and resources
create_resources() {
    echo -e "${YELLOW}Creating Kubernetes resources...${NC}"

    # Create namespace
    echo "→ Creating namespace..."
    kubectl apply -f "$K8S_DIR/namespace.yaml"

    # Create ConfigMaps
    echo "→ Creating ConfigMaps..."
    kubectl apply -f "$K8S_DIR/configmap.yaml"

    # Create PVCs
    echo "→ Creating Persistent Volume Claims..."
    kubectl apply -f "$K8S_DIR/pvc.yaml"

    # Check if secret.yaml exists (not the template)
    if [ -f "$K8S_DIR/secret.yaml" ]; then
        echo "→ Creating Secrets..."
        kubectl apply -f "$K8S_DIR/secret.yaml"
    else
        echo -e "${RED}⚠ Warning: k8s/secret.yaml not found${NC}"
        echo "  Copy k8s/secret.yaml.example to k8s/secret.yaml and fill in credentials"
        echo "  Or create secrets manually with kubectl create secret"
    fi

    echo ""
    echo -e "${GREEN}✓ Resources created${NC}"
}

# Function: Run build job
run_build() {
    echo -e "${YELLOW}Starting build job...${NC}"

    kubectl apply -f "$K8S_DIR/build-job.yaml"

    echo "→ Waiting for build job to complete (timeout: 15 minutes)..."
    kubectl wait --for=condition=complete --timeout=15m job/build-job -n $NAMESPACE

    echo ""
    echo -e "${GREEN}✓ Build completed${NC}"
}

# Function: Run test job
run_tests() {
    echo -e "${YELLOW}Starting test job...${NC}"

    kubectl apply -f "$K8S_DIR/test-job.yaml"

    echo "→ Waiting for test job to complete (timeout: 10 minutes)..."
    kubectl wait --for=condition=complete --timeout=10m job/test-job -n $NAMESPACE

    echo ""
    echo -e "${GREEN}✓ Tests completed${NC}"

    # Extract test results
    echo ""
    echo -e "${YELLOW}Extracting test artifacts...${NC}"

    # Get pod name
    POD_NAME=$(kubectl get pods -n $NAMESPACE -l app.kubernetes.io/component=test --sort-by=.metadata.creationTimestamp -o jsonpath='{.items[-1].metadata.name}')

    if [ -n "$POD_NAME" ]; then
        echo "→ Pod: $POD_NAME"

        # Copy coverage report
        echo "→ Copying coverage report..."
        kubectl cp $NAMESPACE/$POD_NAME:/artifacts/coverage-report ./coverage-report -c test-runner || echo "  (no coverage report found)"

        # Copy test results XML
        echo "→ Copying test results..."
        kubectl cp $NAMESPACE/$POD_NAME:/artifacts/ ./k8s-test-results -c test-runner || echo "  (no test results found)"

        echo ""
        echo -e "${GREEN}✓ Artifacts extracted${NC}"
        echo "  Coverage report: ./coverage-report/index.html"
        echo "  Test results: ./k8s-test-results/"
    fi
}

# Function: Show logs
show_logs() {
    local job_name=$1
    echo -e "${YELLOW}Showing logs for $job_name...${NC}"
    echo ""

    # Get pod name for the job
    POD_NAME=$(kubectl get pods -n $NAMESPACE -l job-name=$job_name --sort-by=.metadata.creationTimestamp -o jsonpath='{.items[-1].metadata.name}')

    if [ -z "$POD_NAME" ]; then
        echo -e "${RED}ERROR: No pod found for job $job_name${NC}"
        return 1
    fi

    echo "Pod: $POD_NAME"
    echo ""

    # Show logs based on job type
    if [ "$job_name" == "build-job" ]; then
        kubectl logs -n $NAMESPACE $POD_NAME -c builder
    elif [ "$job_name" == "test-job" ]; then
        echo -e "${BLUE}=== Test Runner Logs ===${NC}"
        kubectl logs -n $NAMESPACE $POD_NAME -c test-runner

        echo ""
        echo -e "${BLUE}=== D-Bus Daemon Logs ===${NC}"
        kubectl logs -n $NAMESPACE $POD_NAME -c dbus-daemon || true

        echo ""
        echo -e "${BLUE}=== Xvfb Logs ===${NC}"
        kubectl logs -n $NAMESPACE $POD_NAME -c xvfb || true
    fi
}

# Function: Cleanup
cleanup() {
    echo -e "${YELLOW}Cleaning up Kubernetes resources...${NC}"

    echo "→ Deleting jobs..."
    kubectl delete job build-job -n $NAMESPACE --ignore-not-found=true
    kubectl delete job test-job -n $NAMESPACE --ignore-not-found=true

    echo "→ Deleting ConfigMaps..."
    kubectl delete -f "$K8S_DIR/configmap.yaml" --ignore-not-found=true

    echo "→ Deleting PVCs (this will delete cached data)..."
    kubectl delete -f "$K8S_DIR/pvc.yaml" --ignore-not-found=true

    echo "→ Deleting namespace..."
    kubectl delete namespace $NAMESPACE --ignore-not-found=true

    echo ""
    echo -e "${GREEN}✓ Cleanup completed${NC}"
}

# Main logic
case "$COMMAND" in
    create)
        create_resources
        ;;

    build)
        create_resources
        run_build
        ;;

    test)
        create_resources
        run_build
        run_tests
        ;;

    logs)
        JOB_NAME="${2:-test-job}"
        show_logs "$JOB_NAME"
        ;;

    cleanup)
        cleanup
        ;;

    full)
        echo -e "${BLUE}Running full CI/CD pipeline in Kubernetes...${NC}"
        echo ""
        create_resources
        run_build
        run_tests
        echo ""
        echo -e "${GREEN}======================================"
        echo "Pipeline completed successfully!"
        echo "======================================${NC}"
        ;;

    *)
        echo -e "${RED}ERROR: Unknown command '$COMMAND'${NC}"
        echo ""
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  create   - Create namespace and resources only"
        echo "  build    - Create resources and run build job"
        echo "  test     - Create resources, run build, and run tests"
        echo "  full     - Run complete pipeline (create + build + test)"
        echo "  logs [job-name]  - Show logs for job (default: test-job)"
        echo "  cleanup  - Delete all resources"
        echo ""
        echo "Examples:"
        echo "  $0 full              # Run complete pipeline"
        echo "  $0 logs build-job    # View build logs"
        echo "  $0 logs test-job     # View test logs"
        echo "  $0 cleanup           # Clean up everything"
        exit 1
        ;;
esac
