module Registry
  HANDLERS = []
  def self.register(name) = HANDLERS << name

  # a def evaluates to its name, so this handler is referenced
  register def self.handle_csv(row) = row.split(",")

  # never registered, never called
  def self.handle_tsv(row) = row.split("\t")
end
