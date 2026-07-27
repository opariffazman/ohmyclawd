package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

type Creds struct {
	AccessToken  string
	RefreshToken string
	ExpiresAt    int64 // milliseconds since epoch (matches Claude Code file)
	Scopes       []string
}

type credsFile struct {
	ClaudeAiOauth *struct {
		AccessToken  string   `json:"accessToken"`
		RefreshToken string   `json:"refreshToken"`
		ExpiresAt    int64    `json:"expiresAt"`
		Scopes       []string `json:"scopes"`
	} `json:"claudeAiOauth"`
}

const macOSKeychainService = "Claude Code-credentials"

// LoadCreds reads the Claude Code credentials file at path.
// Returns a wrapped os.ErrNotExist when the file is absent so callers can
// distinguish "not yet provisioned" from a parse error.
func LoadCreds(path string) (*Creds, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err // includes os.ErrNotExist on missing
	}
	return parseCreds(data, path)
}

// LoadDefaultCreds reads the legacy credentials file and, on macOS, falls
// back to the Keychain service used by current Claude Code releases.
func LoadDefaultCreds() (*Creds, error) {
	return loadDefaultCreds(defaultCredsPath(), runtime.GOOS, LoadCreds, LoadMacOSKeychainCreds)
}

func loadDefaultCreds(path, goos string, fileLoader func(string) (*Creds, error), keychainLoader func() (*Creds, error)) (*Creds, error) {
	creds, err := fileLoader(path)
	if err == nil || goos != "darwin" || !errors.Is(err, os.ErrNotExist) {
		return creds, err
	}
	return keychainLoader()
}

// LoadMacOSKeychainCreds reads the OAuth credentials stored by Claude Code.
func LoadMacOSKeychainCreds() (*Creds, error) {
	data, err := exec.Command("/usr/bin/security", "find-generic-password", "-s", macOSKeychainService, "-w").Output()
	if err != nil {
		return nil, fmt.Errorf("read macOS Keychain service %q: %w", macOSKeychainService, err)
	}
	return parseCreds(data, "macOS Keychain")
}

func defaultCredsPath() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return ".credentials.json"
	}
	return filepath.Join(home, ".claude", ".credentials.json")
}

func parseCreds(data []byte, source string) (*Creds, error) {
	var f credsFile
	if err := json.Unmarshal(data, &f); err != nil {
		return nil, fmt.Errorf("parse %s: %w", source, err)
	}
	if f.ClaudeAiOauth == nil || f.ClaudeAiOauth.AccessToken == "" {
		return nil, fmt.Errorf("%s: missing claudeAiOauth.accessToken", source)
	}
	return &Creds{
		AccessToken:  f.ClaudeAiOauth.AccessToken,
		RefreshToken: f.ClaudeAiOauth.RefreshToken,
		ExpiresAt:    f.ClaudeAiOauth.ExpiresAt,
		Scopes:       f.ClaudeAiOauth.Scopes,
	}, nil
}
