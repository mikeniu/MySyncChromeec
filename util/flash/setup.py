import setuptools

setuptools.setup(
    name="flash_ec",
    version="1.0.0",
    author="The ChromiumOS Authors",
    description="Python rewrite of the ChromeOS EC flash tool",
    packages=setuptools.find_packages(),
    python_requires=">=3.8",
)
