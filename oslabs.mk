export TOKEN := ???

# ----- DO NOT MODIFY -----

export COURSE := OS2024
URL := 'https://jyywiki.cn/submit.sh'

submit:
	@cd $(dir $(abspath $(lastword $(MAKEFILE_LIST)))) && \
	  curl -sSLf '$(URL)' > /dev/null && \
	  curl -sSLf '$(URL)' | bash

git:
	@find ../.shadow/ -maxdepth 1 -type d -name '[a-z]*' | xargs rm -rf
	@cp -r `find .. -maxdepth 1 -type d -name '[a-z]*'` ../.shadow/
	@git add ../.shadow -A --ignore-errors
	@while (test -e .git/index.lock); do sleep 0.1; done
	@(uname -a && uptime) | git commit -F - -q --author='tracer-nju <tracer@nju.edu.cn>' --no-verify --allow-empty
	@sync
