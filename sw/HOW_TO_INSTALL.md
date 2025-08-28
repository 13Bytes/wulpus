# How to install Python requirements?

1. Install Anaconda package manager<br>
   https://docs.conda.io/en/latest/miniconda.html
2. Find `requirements.yaml` file in this folder
3. Open terminal (Windows: Anaconda Prompt) in this folder.
4. Execute the following command to create environment:

```
    conda env create -f requirements.yml
```

# How to install Node.js and requirements?

1. Install current version of Node.js<br>
   https://nodejs.org/en/#home-downloadhead
2. Open Terminal in `\sw\wulpus-frontend`
3. Execute the following command to install all npm dependencies:

```
    npm i
```

### Initial build

- Open Terminal in `\sw\wulpus-frontend`
- Execute the following command to build the frontend and copy the results into the right folder:

```
    python build.py
```
