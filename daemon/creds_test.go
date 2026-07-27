package main

import (
	"errors"
	"os"
	"testing"
)

func TestLoadCredsValid(t *testing.T) {
	c, err := LoadCreds("testdata/creds_valid.json")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if c.AccessToken != "sk-ant-oat01-VALIDTOKEN" {
		t.Fatalf("AccessToken = %q", c.AccessToken)
	}
	if c.ExpiresAt != 9999999999000 {
		t.Fatalf("ExpiresAt = %d", c.ExpiresAt)
	}
}

func TestLoadCredsMissingFile(t *testing.T) {
	_, err := LoadCreds("testdata/does_not_exist.json")
	if !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("expected ErrNotExist, got %v", err)
	}
}

func TestLoadCredsMalformed(t *testing.T) {
	_, err := LoadCreds("testdata/creds_bogus.json")
	if err == nil {
		t.Fatal("expected error for missing claudeAiOauth")
	}
}

func TestLoadDefaultCredsPrefersFile(t *testing.T) {
	fileCreds := &Creds{AccessToken: "file-token"}
	keychainCalled := false

	got, err := loadDefaultCreds("unused", "darwin",
		func(string) (*Creds, error) { return fileCreds, nil },
		func() (*Creds, error) {
			keychainCalled = true
			return &Creds{AccessToken: "keychain-token"}, nil
		},
	)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != fileCreds {
		t.Fatalf("got %#v, want file credentials", got)
	}
	if keychainCalled {
		t.Fatal("keychain loader called when credentials file exists")
	}
}

func TestLoadDefaultCredsUsesMacOSKeychainWhenLegacyFileIsMissing(t *testing.T) {
	keychainCreds := &Creds{AccessToken: "keychain-token"}
	keychainCalled := false

	got, err := loadDefaultCreds("unused", "darwin",
		func(string) (*Creds, error) { return nil, os.ErrNotExist },
		func() (*Creds, error) {
			keychainCalled = true
			return keychainCreds, nil
		},
	)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != keychainCreds {
		t.Fatalf("got %#v, want Keychain credentials", got)
	}
	if !keychainCalled {
		t.Fatal("keychain loader was not called")
	}
}

func TestLoadDefaultCredsLeavesMissingLegacyFileUnchangedOnLinux(t *testing.T) {
	keychainCalled := false
	_, err := loadDefaultCreds("unused", "linux",
		func(string) (*Creds, error) { return nil, os.ErrNotExist },
		func() (*Creds, error) {
			keychainCalled = true
			return nil, nil
		},
	)
	if !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("expected ErrNotExist, got %v", err)
	}
	if keychainCalled {
		t.Fatal("keychain loader called on Linux")
	}
}
