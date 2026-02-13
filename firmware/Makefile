prep-public:
	@echo "Preparing public directory..."
	@rm -rf public/*.gz
	@gzip -k public/src/*
	@mv public/src/*.gz public/
