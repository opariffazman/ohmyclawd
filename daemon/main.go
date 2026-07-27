package main

import (
	"context"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

func main() {
	listen := getenv("OHMYCLAWD_LISTEN", "127.0.0.1:8787")
	token := getenv("OHMYCLAWD_TOKEN", "")
	probeInterval := getenvDur("OHMYCLAWD_PROBE_INTERVAL", 60*time.Second)
	anthropicURL := getenv("OHMYCLAWD_ANTHROPIC_URL", "https://api.anthropic.com/v1/messages")
	fakeMode := flag.Bool("fake", false, "serve a scripted Usage curve, no Anthropic calls")
	flag.Parse()
	loadCreds := LoadDefaultCreds
	if credsPath := os.Getenv("OHMYCLAWD_CREDS_PATH"); credsPath != "" {
		loadCreds = func() (*Creds, error) { return LoadCreds(credsPath) }
	}

	state := NewState()
	metrics := NewMetrics()
	tmux := NewTmuxWatcher()
	handler := NewHandler(state, metrics, tmux, time.Now, token)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go tmux.Run(ctx)

	if *fakeMode {
		go runFake(ctx, state)
		log.Printf("ohmyclawd-daemon listening on %s (fake mode)", listen)
	} else {
		creds, err := loadCreds()
		if err != nil {
			log.Fatalf("credentials: %v", err)
		}
		log.Printf("loaded OAuth token, expires unix-ms=%d", creds.ExpiresAt)
		prober := &Prober{
			URL:   anthropicURL,
			Token: creds.AccessToken,
			HTTP:  &http.Client{Timeout: 30 * time.Second},
		}
		cfg := LoopConfig{
			Base:        probeInterval,
			RateLimited: 5 * time.Minute,
			Backoff:     []time.Duration{60 * time.Second, 120 * time.Second, 240 * time.Second, 480 * time.Second, 600 * time.Second},
			ReloadCreds: loadCreds,
		}
		go RunLoop(ctx, prober, state, metrics, cfg)
		log.Printf("ohmyclawd-daemon listening on %s (probing %s every %s)", listen, anthropicURL, probeInterval)
	}

	srv := &http.Server{Addr: listen, Handler: handler, ReadHeaderTimeout: 5 * time.Second}
	go func() {
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("listen: %v", err)
		}
	}()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	log.Println("shutting down")
	cancel()
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer shutdownCancel()
	_ = srv.Shutdown(shutdownCtx)
}

func getenv(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}

func getenvDur(k string, d time.Duration) time.Duration {
	if v := os.Getenv(k); v != "" {
		if dur, err := time.ParseDuration(v); err == nil {
			return dur
		}
	}
	return d
}
